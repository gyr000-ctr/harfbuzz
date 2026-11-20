/*
 * Copyright © 2026  Behdad Esfahbod
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#include "hb-test.h"

#include <hb-ot.h>
#include <hb-subset.h>

/* avar2 partial-instancing region culling.
 *
 * TestAvar2Cull.ttf is TestAvar2Instance.ttf plus one extra avar2 VarStore
 * region whose only nonzero tent is a one-sided tent on the hidden axis
 * XTRA. A hidden axis's intermediate coordinate (the avar2 VarStore's input)
 * is always 0, so that region's scalar is identically zero at every valid
 * location: it is unreachable and the partial instancer must cull it.
 * Dropping it changes neither final normalized coordinates nor rendering,
 * while making the output avar table strictly smaller.
 */

#define WGHT HB_TAG ('w','g','h','t')
#define WDTH HB_TAG ('w','d','t','h')
#define XTRA HB_TAG ('X','T','R','A')

static unsigned
read_uint16_be (const unsigned char *p)
{
  return ((unsigned) p[0] << 8) | p[1];
}

static unsigned
read_uint32_be (const unsigned char *p)
{
  return ((unsigned) p[0] << 24) | ((unsigned) p[1] << 16) |
	 ((unsigned) p[2] << 8) | p[3];
}

/* Locate the avar2 VarRegionList inside an avar table blob and report its
 * axis/region counts. Returns false when the table has no v2 VarStore. */
static bool
get_avar2_region_list (hb_blob_t *avar_blob,
		       unsigned *region_axis_count,
		       unsigned *region_count,
		       const unsigned char **region_list /* OUT */)
{
  unsigned length;
  const unsigned char *d = (const unsigned char *) hb_blob_get_data (avar_blob, &length);
  if (length < 8) return false;
  if (read_uint16_be (d) < 2) return false; /* needs an avar v2 table */

  unsigned axis_count = read_uint16_be (d + 6);
  unsigned offset = 8;
  for (unsigned i = 0; i < axis_count; i++)
  {
    if (offset + 2 > length) return false;
    unsigned map_count = read_uint16_be (d + offset);
    offset += 2 + map_count * 4;
    if (offset > length) return false;
  }
  if (offset + 8 > length) return false;

  unsigned var_store_offset = read_uint32_be (d + offset + 4);
  if (!var_store_offset || var_store_offset + 10 > length) return false;

  const unsigned char *var_store = d + var_store_offset;
  unsigned region_list_offset = read_uint32_be (var_store + 2);
  if (var_store_offset + region_list_offset + 4 > length) return false;

  const unsigned char *vrl = d + var_store_offset + region_list_offset;
  *region_axis_count = read_uint16_be (vrl);
  *region_count = read_uint16_be (vrl + 2);
  *region_list = vrl;
  return true;
}

static hb_face_t *
open_cull_font (void)
{
  return hb_test_open_font_file ("fonts/TestAvar2Cull.ttf");
}

static hb_subset_input_t *
create_input (void)
{
  hb_subset_input_t *input = hb_subset_input_create_or_fail ();
  g_assert_nonnull (input);
  hb_subset_input_keep_everything (input);
  return input;
}

/* Partial-instance the cull font, restricting wght to [300, 500]. */
static hb_face_t *
instance_cull_font (hb_face_t *face)
{
  hb_subset_input_t *input = create_input ();
  g_assert_true (hb_subset_input_set_axis_range (input, face, WGHT,
						 300.f, 500.f, 400.f));
  hb_face_t *inst = hb_subset_or_fail (face, input);
  hb_subset_input_destroy (input);
  g_assert_nonnull (inst);
  return inst;
}

/* The unreachable XTRA region must be culled: fewer regions than the source,
 * and no region keeps a tent on the hidden XTRA axis. */
static void
test_avar2_cull_drops_unreachable_region (void)
{
  hb_face_t *face = open_cull_font ();
  hb_blob_t *src_avar = hb_face_reference_table (face, HB_TAG ('a','v','a','r'));

  /* Resolve the hidden XTRA axis's fvar position through the public API. */
  unsigned axis_count = hb_ot_var_get_axis_count (face);
  hb_ot_var_axis_info_t *axes = g_new0 (hb_ot_var_axis_info_t, axis_count);
  g_assert_cmpuint (hb_ot_var_get_axis_infos (face, 0, &axis_count, axes),
		    ==, axis_count);
  unsigned xtra_axis = axis_count;
  for (unsigned i = 0; i < axis_count; i++)
    if (axes[i].tag == XTRA)
      xtra_axis = i;
  g_assert_cmpuint (xtra_axis, <, axis_count);
  g_assert_true (axes[xtra_axis].flags & HB_OT_VAR_AXIS_FLAG_HIDDEN);
  g_free (axes);

  unsigned src_axes, src_regions;
  const unsigned char *src_vrl;
  g_assert_true (get_avar2_region_list (src_avar, &src_axes, &src_regions, &src_vrl));
  g_assert_cmpuint (src_regions, ==, 5);
  g_assert_cmpuint (xtra_axis, <, src_axes);

  hb_face_t *inst = instance_cull_font (face);
  hb_blob_t *inst_avar = hb_face_reference_table (inst, HB_TAG ('a','v','a','r'));

  unsigned inst_axes, inst_regions;
  const unsigned char *inst_vrl;
  g_assert_true (get_avar2_region_list (inst_avar, &inst_axes, &inst_regions, &inst_vrl));
  g_assert_cmpuint (inst_axes, ==, src_axes);
  g_assert_cmpuint (inst_regions, <, src_regions);
  g_assert_cmpuint (inst_regions, ==, 4);

  /* No surviving region may have a nonzero XTRA peak: that scalar can never
   * be evaluated because the hidden axis's intermediate coordinate is 0. */
  for (unsigned r = 0; r < inst_regions; r++)
  {
    const unsigned char *region_axes = inst_vrl + 4 + (size_t) r * inst_axes * 6;
    short xtra_peak = (short) read_uint16_be (region_axes + xtra_axis * 6 + 2);
    g_assert_cmpint (xtra_peak, ==, 0);
  }

  hb_blob_destroy (inst_avar);
  hb_blob_destroy (src_avar);
  hb_face_destroy (inst);
  hb_face_destroy (face);
}

/* Culling must preserve behavior: compare final normalized coordinates and
 * the 'A' advance/extents against an instance of the original font (without
 * the dead region), over a grid spanning the retained box. */
static void
test_avar2_cull_preserves_shaping (void)
{
  hb_face_t *cull_face = open_cull_font ();
  hb_face_t *orig_face = hb_test_open_font_file ("fonts/TestAvar2Instance.ttf");
  hb_face_t *cull_inst = instance_cull_font (cull_face);
  hb_face_t *orig_inst = instance_cull_font (orig_face);

  static const float wdths[] = {50.f, 100.f, 150.f, 200.f};
  for (unsigned i = 0; i <= 10; i++)
  {
    float wght = 300.f + (500.f - 300.f) * i / 10.f;
    for (unsigned j = 0; j < G_N_ELEMENTS (wdths); j++)
    {
      hb_variation_t vars[2] = {{WGHT, wght}, {WDTH, wdths[j]}};

      hb_font_t *a = hb_font_create (cull_inst);
      hb_font_t *b = hb_font_create (orig_inst);
      hb_font_set_variations (a, vars, 2);
      hb_font_set_variations (b, vars, 2);

      unsigned na = 0, nb = 0;
      const int *ca = hb_font_get_var_coords_normalized (a, &na);
      const int *cb = hb_font_get_var_coords_normalized (b, &nb);
      g_assert_cmpuint (na, ==, nb);
      for (unsigned k = 0; k < na; k++)
	g_assert_cmpint (ca[k], ==, cb[k]);

      hb_codepoint_t ga = 0, gb = 0;
      g_assert_true (hb_font_get_nominal_glyph (a, 'A', &ga));
      g_assert_true (hb_font_get_nominal_glyph (b, 'A', &gb));
      g_assert_cmpint (hb_font_get_glyph_h_advance (a, ga),
		       ==, hb_font_get_glyph_h_advance (b, gb));

      hb_glyph_extents_t ea, eb;
      g_assert_true (hb_font_get_glyph_extents (a, ga, &ea));
      g_assert_true (hb_font_get_glyph_extents (b, gb, &eb));
      g_assert_cmpint (ea.x_bearing, ==, eb.x_bearing);
      g_assert_cmpint (ea.y_bearing, ==, eb.y_bearing);
      g_assert_cmpint (ea.width, ==, eb.width);
      g_assert_cmpint (ea.height, ==, eb.height);

      hb_font_destroy (a);
      hb_font_destroy (b);
    }
  }

  hb_face_destroy (orig_inst);
  hb_face_destroy (cull_inst);
  hb_face_destroy (orig_face);
  hb_face_destroy (cull_face);
}

/* The culled output must be strictly smaller than the equivalent instance
 * produced when culling is not able to remove the region. Since the dead
 * region is unreachable, subtracting it shrinks the serialized avar table. */
static void
test_avar2_cull_output_smaller (void)
{
  hb_face_t *face = open_cull_font ();
  hb_face_t *inst = instance_cull_font (face);

  hb_blob_t *src_avar = hb_face_reference_table (face, HB_TAG ('a','v','a','r'));
  hb_blob_t *inst_avar = hb_face_reference_table (inst, HB_TAG ('a','v','a','r'));
  g_assert_cmpuint (hb_blob_get_length (inst_avar), <,
		    hb_blob_get_length (src_avar));

  hb_blob_destroy (inst_avar);
  hb_blob_destroy (src_avar);
  hb_face_destroy (inst);
  hb_face_destroy (face);
}

/* Repeated subsetting must produce byte-identical output. */
static void
test_avar2_cull_deterministic (void)
{
  hb_face_t *face = open_cull_font ();

  hb_face_t *first = instance_cull_font (face);
  hb_face_t *second = instance_cull_font (face);

  hb_blob_t *b1 = hb_face_reference_blob (first);
  hb_blob_t *b2 = hb_face_reference_blob (second);
  unsigned l1, l2;
  const char *d1 = hb_blob_get_data (b1, &l1);
  const char *d2 = hb_blob_get_data (b2, &l2);
  g_assert_cmpuint (l1, ==, l2);
  g_assert_cmpint (memcmp (d1, d2, l1), ==, 0);

  hb_blob_destroy (b1);
  hb_blob_destroy (b2);
  hb_face_destroy (second);
  hb_face_destroy (first);
  hb_face_destroy (face);
}

typedef struct
{
  hb_face_t *source;
  bool make_bad;
} bad_cfg_t;

/* Rewrite the first VarData's first region index to an out-of-range value.
 * The region index then names a region the VarRegionList does not contain. */
static hb_blob_t *
reference_table_with_bad_region_index (hb_face_t *face HB_UNUSED,
				       hb_tag_t tag,
				       void *user_data)
{
  bad_cfg_t *cfg = (bad_cfg_t *) user_data;
  hb_blob_t *table = hb_face_reference_table (cfg->source, tag);
  if (tag != HB_TAG ('a','v','a','r') || !cfg->make_bad)
    return table;

  hb_blob_t *copy = hb_blob_copy_writable_or_fail (table);
  hb_blob_destroy (table);
  g_assert_nonnull (copy);

  unsigned length;
  unsigned char *d = (unsigned char *) hb_blob_get_data_writable (copy, &length);
  unsigned axis_count = read_uint16_be (d + 6);
  unsigned offset = 8;
  for (unsigned i = 0; i < axis_count; i++)
  {
    unsigned map_count = read_uint16_be (d + offset);
    offset += 2 + map_count * 4;
  }
  unsigned var_store_offset = read_uint32_be (d + offset + 4);
  g_assert_cmpuint (var_store_offset, <=, length - 12);
  const unsigned char *var_store = d + var_store_offset;
  unsigned var_data_offset = read_uint32_be (var_store + 8);
  g_assert_cmpuint (var_store_offset + var_data_offset, <=, length - 8);
  unsigned char *var_data = d + var_store_offset + var_data_offset;
  /* The source VarData has a nonzero region index count, so the first
   * regionIndex lives at VarData + 6. Corrupt it to a value no region in the
   * VarRegionList can reach. */
  g_assert_cmpuint (read_uint16_be (var_data + 4) & 0x7FFFu, >, 0u);
  var_data[6] = 0xFF;
  var_data[7] = 0xFF;
  return copy;
}

/* A malformed avar2 VarStore whose VarData names an out-of-range region must
 * be rejected with a distinct failure (null subset result), never crashing. */
static void
test_avar2_cull_bad_region_index_fails (void)
{
  hb_face_t *source = hb_test_open_font_file ("fonts/TestAvar2Instance.ttf");
  bad_cfg_t good_cfg = {source, false};
  bad_cfg_t bad_cfg = {source, true};

  hb_face_t *good_face = hb_face_create_for_tables (
      reference_table_with_bad_region_index, &good_cfg, NULL);
  hb_face_t *bad_face = hb_face_create_for_tables (
      reference_table_with_bad_region_index, &bad_cfg, NULL);

  hb_subset_input_t *good_input = create_input ();
  g_assert_true (hb_subset_input_set_axis_range (good_input, good_face,
						  WGHT, 300.f, 500.f, 400.f));
  hb_face_t *good_inst = hb_subset_or_fail (good_face, good_input);
  hb_subset_input_destroy (good_input);
  g_assert_nonnull (good_inst);

  hb_subset_input_t *bad_input = create_input ();
  g_assert_true (hb_subset_input_set_axis_range (bad_input, bad_face,
						  WGHT, 300.f, 500.f, 400.f));
  hb_face_t *bad_inst = hb_subset_or_fail (bad_face, bad_input);
  hb_subset_input_destroy (bad_input);
  g_assert_null (bad_inst);

  hb_face_destroy (good_inst);
  hb_face_destroy (bad_face);
  hb_face_destroy (good_face);
  hb_face_destroy (source);
}

int
main (int argc, char **argv)
{
  hb_test_init (&argc, &argv);

  hb_test_add (test_avar2_cull_drops_unreachable_region);
  hb_test_add (test_avar2_cull_preserves_shaping);
  hb_test_add (test_avar2_cull_output_smaller);
  hb_test_add (test_avar2_cull_deterministic);
  hb_test_add (test_avar2_cull_bad_region_index_fails);

  return hb_test_run ();
}
