"""Generate an avar2 font whose VarStore carries an unreachable region.

The source TestAvar2Instance.ttf has a hidden fvar axis, XTRA, whose
intermediate (post-fvar, post-avar-v1, pre-avar-v2) coordinate is always 0,
so the runtime never feeds the avar2 VarStore a nonzero XTRA value. Appending
a region whose only nonzero tent is a one-sided XTRA tent therefore makes a
region the VarStore can never reach: its scalar is identically zero at every
valid location. A correct partial-instancing cull must drop it, while every
other table and every reachable coordinate stay byte-for-byte equivalent.

The new region is referenced with a nonzero delta so it is a real, populated
region rather than an empty slot; the delta simply never contributes.
"""

from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables import otTables


SOURCE = "TestAvar2Instance.ttf"
OUTPUT = "TestAvar2Cull.ttf"


def main():
    font = TTFont(Path(__file__).parent / SOURCE)
    avar2 = font["avar"].table
    var_store = avar2.VarStore
    region_list = var_store.VarRegionList
    axis_tags = [axis.axisTag for axis in font["fvar"].axes]
    hidden_index = axis_tags.index("XTRA")

    # One-sided positive tent on the hidden axis; zero tent on every other
    # axis, so the region scalar is nonzero only for positive XTRA values that
    # the runtime can never produce.
    region = otTables.VarRegion()
    region.VarRegionAxis = []
    for i in range(len(axis_tags)):
        axis = otTables.VarRegionAxis()
        if i == hidden_index:
            axis.StartCoord, axis.PeakCoord, axis.EndCoord = 0.0, 1.0, 1.0
        else:
            axis.StartCoord = axis.PeakCoord = axis.EndCoord = 0.0
        region.VarRegionAxis.append(axis)

    new_region_index = region_list.RegionCount
    region_list.Region.append(region)
    region_list.RegionCount = len(region_list.Region)

    # Reference the new region from the first VarData and give every row a
    # column for it; only the first row's value is nonzero.
    var_data = var_store.VarData[0]
    var_data.VarRegionIndex.append(new_region_index)
    var_data.VarRegionCount = len(var_data.VarRegionIndex)
    for row in var_data.Item:
        row.append(0)
    var_data.Item[0][-1] = 100
    var_data.ItemCount = len(var_data.Item)
    var_data.calculateNumShorts(optimize=True)
    var_store.VarDataCount = len(var_store.VarData)

    font.recalcTimestamp = False
    font["head"].created = font["head"].modified = 2082844800
    font.save(Path(__file__).parent / OUTPUT)


if __name__ == "__main__":
    main()
