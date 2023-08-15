/*
 * kmscon - OpenGL Textures Text Renderer Backend
 *
 * Copyright (c) 2012-2013 David Herrmann <dh.herrmann@googlemail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * SECTION:text_gltex.c
 * @short_description: OpenGL Textures Text Renderer Backend
 * @include: text.h
 *
 * Uses OpenGL textures to store glyph information and draws these textures with
 * a custom fragment shader.
 * Glyphs are stored in texture-atlases. OpenGL has heavy restrictions on
 * texture sizes so we need to use multiple atlases. As there is no way to pass
 * a varying amount of textures to a shader, we need to render the screen for
 * each atlas we have.
 */

#define GL_GLEXT_PROTOTYPES

#include <errno.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "shl_dlist.h"
#include "shl_gl.h"
#include "shl_hashtable.h"
#include "shl_log.h"
#include "shl_misc.h"
#include "text.h"
#include "uterm_video.h"
#include "text_gltex_atlas.frag.bin.h"
#include "text_gltex_atlas.vert.bin.h"
#include "mouse_pointer.frag.bin.h"
#include "mouse_pointer.vert.bin.h"
#include "background.vert.bin.h"
#include "background-1.frag.bin.h"
#include "background-2.frag.bin.h"
#include "background-3.frag.bin.h"
#include "background-4.frag.bin.h"
#include "background-5.frag.bin.h"
#include "background-6.frag.bin.h"
#include "background-7.frag.bin.h"

#define LOG_SUBSYSTEM "text_gltex"

/* thanks khronos for breaking backwards compatibility.. */
#if !defined(GL_UNPACK_ROW_LENGTH) && defined(GL_UNPACK_ROW_LENGTH_EXT)
#  define GL_UNPACK_ROW_LENGTH GL_UNPACK_ROW_LENGTH_EXT
#endif

struct atlas {
	struct shl_dlist list;

	GLuint tex;
	unsigned int height;
	unsigned int width;
	unsigned int count;
	unsigned int fill;

	unsigned int cache_size;
	unsigned int cache_num;
	GLfloat *cache_pos;
	GLfloat *cache_texpos;
	GLfloat *cache_fgcol;
	GLfloat *cache_bgcol;

	GLfloat advance_htex;
	GLfloat advance_vtex;
};

struct glyph {
	const struct kmscon_glyph *glyph;
	struct atlas *atlas;
	unsigned int texoff;
};

#define GLYPH_WIDTH(gly) ((gly)->glyph->buf.width)
#define GLYPH_HEIGHT(gly) ((gly)->glyph->buf.height)
#define GLYPH_STRIDE(gly) ((gly)->glyph->buf.stride)
#define GLYPH_DATA(gly) ((gly)->glyph->buf.data)

GLfloat mouse_block_vertices[] = {-.01f,  .02f,  // 0) top-left
								   .01f,  .02f,  // 1) top-right
								  -.01f, -.02f,  // 2) bottom-left
								   .01f, -.02f}; // 3) bottom-right

GLuint mouse_block_outline_indices[] = {0, 1,
										1, 3,
										0, 2,
										2, 3};

GLuint mouse_block_fill_indices[] = {0, 1, 3,
									 0, 3, 2};

GLfloat background_vertices[] = {-1.f,  1.f,  // 0) top-left
								  1.f,  1.f,  // 1) top-right
								 -1.f, -1.f,  // 2) bottom-left
								  1.f, -1.f}; // 3) bottom-right

GLfloat background_tex_coords[] = { .0f, 1.f,   // 0) top-left
								   1.f,  1.f,   // 1) top-right
									.0f,  .0f,  // 2) bottom-left
								   1.f,   .0f}; // 3) bottom-right

GLuint background_indices[] = {0, 1, 3,
							   0, 3, 2};

struct gltex {
	struct shl_hashtable *glyphs;
	struct shl_hashtable *bold_glyphs;
	unsigned int max_tex_size;
	bool supports_rowlen;

	struct shl_dlist atlases;

	GLfloat advance_x;
	GLfloat advance_y;

	struct gl_shader *shader;
	GLuint uni_orientation;
	GLuint uni_proj;
	GLuint uni_atlas;
	GLuint uni_advance_htex;
	GLuint uni_advance_vtex;

	unsigned int sw;
	unsigned int sh;

	GLfloat angle;

	struct gl_shader *mouse_pointer_shader;
	GLuint uni_orientation_mouse;
	GLuint uni_proj_mouse;
	GLuint uni_color_mouse;
	GLuint uni_offset_mouse;

	struct gl_shader *background_shader[7];
	GLuint uni_res_background[7];
	GLuint uni_time_background[7];
	GLuint uni_proj_background[7];
};

#define FONT_WIDTH(txt) ((txt)->font->attr.width)
#define FONT_HEIGHT(txt) ((txt)->font->attr.height)

static int gltex_init(struct kmscon_text *txt)
{
	struct gltex *gt;

	gt = malloc(sizeof(*gt));
	if (!gt)
		return -ENOMEM;

	txt->data = gt;
	return 0;
}

static void gltex_destroy(struct kmscon_text *txt)
{
	struct gltex *gt = txt->data;

	free(gt);
}

static void free_glyph(void *data)
{
	struct glyph *glyph = data;

	free(glyph);
}

static int gltex_set(struct kmscon_text *txt)
{
	struct gltex *gt = txt->data;
	int ret, vlen, flen;
	const char *vert, *frag;
	static char *attr[] = { "position", "texture_position",
				"fgcolor", "bgcolor" };
	GLint s;
	const char *ext;
	struct uterm_mode *mode;
	bool opengl;

	memset(gt, 0, sizeof(*gt));
	shl_dlist_init(&gt->atlases);

	ret = shl_hashtable_new(&gt->glyphs, shl_direct_hash,
				shl_direct_equal, free_glyph);
	if (ret)
		return ret;

	ret = shl_hashtable_new(&gt->bold_glyphs, shl_direct_hash,
				shl_direct_equal, free_glyph);
	if (ret)
		goto err_htable;

	ret = uterm_display_use(txt->disp, &opengl);
	if (ret < 0 || !opengl) {
		if (ret == -EOPNOTSUPP)
			log_error("display doesn't support hardware-acceleration");
		goto err_bold_htable;
	}

	vert = _binary_text_gltex_atlas_vert_start;
	vlen = _binary_text_gltex_atlas_vert_size;
	frag = _binary_text_gltex_atlas_frag_start;
	flen = _binary_text_gltex_atlas_frag_size;
	gl_clear_error();

	ret = gl_shader_new(&gt->shader, vert, vlen, frag, flen, attr, 4,
			    log_llog, NULL);
	if (ret)
		goto err_bold_htable;

	gt->uni_orientation = gl_shader_get_uniform(gt->shader, "orientation");
	gt->uni_proj = gl_shader_get_uniform(gt->shader, "projection");
	gt->uni_atlas = gl_shader_get_uniform(gt->shader, "atlas");
	gt->uni_advance_htex = gl_shader_get_uniform(gt->shader,
						     "advance_htex");
	gt->uni_advance_vtex = gl_shader_get_uniform(gt->shader,
						     "advance_vtex");

	if (gl_has_error(gt->shader)) {
		log_warning("cannot create shader");
		goto err_shader;
	}

	vert = _binary_mouse_pointer_vert_start;
	vlen = _binary_mouse_pointer_vert_size;
	frag = _binary_mouse_pointer_frag_start;
	flen = _binary_mouse_pointer_frag_size;
	gl_clear_error();
	static char *mouse_pointer_attr[] = { "position" };
	ret = gl_shader_new(&gt->mouse_pointer_shader,
						vert, vlen,
						frag, flen,
						mouse_pointer_attr, 1,
						log_llog, NULL);
	if (ret)
		goto err_shader;

	gt->uni_orientation_mouse = gl_shader_get_uniform(gt->mouse_pointer_shader,
													  "orientation");
	gt->uni_proj_mouse = gl_shader_get_uniform(gt->mouse_pointer_shader,
											   "projection");
	gt->uni_color_mouse = gl_shader_get_uniform(gt->mouse_pointer_shader,
												"color");
	gt->uni_offset_mouse = gl_shader_get_uniform(gt->mouse_pointer_shader,
												 "offset");

	if (gl_has_error(gt->mouse_pointer_shader)) {
		log_warning("cannot create mouse-pointer shader");
		goto err_mouse_pointer_shader;
	}

	/** background start *********/
	vert = _binary_background_vert_start;
	vlen = _binary_background_vert_size;
	frag = _binary_background_1_frag_start;
	flen = _binary_background_1_frag_size;
	gl_clear_error();
	static char *background_attr[] = { "position", "texture_position" };
	ret = gl_shader_new(&gt->background_shader[0],
						vert, vlen,
						frag, flen,
						background_attr, 1,
						log_llog, NULL);
	if (ret)
		goto err_shader;

	gt->uni_res_background[0] = gl_shader_get_uniform(gt->background_shader[0], "resolution");
	gt->uni_time_background[0] = gl_shader_get_uniform(gt->background_shader[0], "time");
	gt->uni_proj_background[0] = gl_shader_get_uniform(gt->background_shader[0], "projection");

	if (gl_has_error(gt->background_shader[0])) {
		log_warning("cannot create background shader 1");
		goto err_background_shader;
	}

	frag = _binary_background_2_frag_start;
	flen = _binary_background_2_frag_size;
	gl_clear_error();
	ret = gl_shader_new(&gt->background_shader[1],
						vert, vlen,
						frag, flen,
						background_attr, 1,
						log_llog, NULL);
	if (ret)
		goto err_shader;

	gt->uni_res_background[1] = gl_shader_get_uniform(gt->background_shader[1], "resolution");
	gt->uni_time_background[1] = gl_shader_get_uniform(gt->background_shader[1], "time");
	gt->uni_proj_background[1] = gl_shader_get_uniform(gt->background_shader[1], "projection");

	if (gl_has_error(gt->background_shader[1])) {
		log_warning("cannot create background shader 2");
		goto err_background_shader;
	}

	frag = _binary_background_3_frag_start;
	flen = _binary_background_3_frag_size;
	gl_clear_error();
	ret = gl_shader_new(&gt->background_shader[2],
						vert, vlen,
						frag, flen,
						background_attr, 1,
						log_llog, NULL);
	if (ret)
		goto err_shader;

	gt->uni_res_background[2] = gl_shader_get_uniform(gt->background_shader[2], "resolution");
	gt->uni_time_background[2] = gl_shader_get_uniform(gt->background_shader[2], "time");
	gt->uni_proj_background[2] = gl_shader_get_uniform(gt->background_shader[2], "projection");

	if (gl_has_error(gt->background_shader[2])) {
		log_warning("cannot create background shader 3");
		goto err_background_shader;
	}

	frag = _binary_background_4_frag_start;
	flen = _binary_background_4_frag_size;
	gl_clear_error();
	ret = gl_shader_new(&gt->background_shader[3],
						vert, vlen,
						frag, flen,
						background_attr, 1,
						log_llog, NULL);
	if (ret)
		goto err_shader;

	gt->uni_res_background[3] = gl_shader_get_uniform(gt->background_shader[3], "resolution");
	gt->uni_time_background[3] = gl_shader_get_uniform(gt->background_shader[3], "time");
	gt->uni_proj_background[3] = gl_shader_get_uniform(gt->background_shader[3], "projection");

	if (gl_has_error(gt->background_shader[3])) {
		log_warning("cannot create background shader 4");
		goto err_background_shader;
	}

	frag = _binary_background_5_frag_start;
	flen = _binary_background_5_frag_size;
	gl_clear_error();
	ret = gl_shader_new(&gt->background_shader[4],
						vert, vlen,
						frag, flen,
						background_attr, 1,
						log_llog, NULL);
	if (ret)
		goto err_shader;

	gt->uni_res_background[4] = gl_shader_get_uniform(gt->background_shader[4], "resolution");
	gt->uni_time_background[4] = gl_shader_get_uniform(gt->background_shader[4], "time");
	gt->uni_proj_background[4] = gl_shader_get_uniform(gt->background_shader[4], "projection");

	if (gl_has_error(gt->background_shader[4])) {
		log_warning("cannot create background shader 5");
		goto err_background_shader;
	}

	frag = _binary_background_6_frag_start;
	flen = _binary_background_6_frag_size;
	gl_clear_error();
	ret = gl_shader_new(&gt->background_shader[5],
						vert, vlen,
						frag, flen,
						background_attr, 1,
						log_llog, NULL);
	if (ret)
		goto err_shader;

	gt->uni_res_background[5] = gl_shader_get_uniform(gt->background_shader[5], "resolution");
	gt->uni_time_background[5] = gl_shader_get_uniform(gt->background_shader[5], "time");
	gt->uni_proj_background[5] = gl_shader_get_uniform(gt->background_shader[5], "projection");

	if (gl_has_error(gt->background_shader[5])) {
		log_warning("cannot create background shader 6");
		goto err_background_shader;
	}

	frag = _binary_background_7_frag_start;
	flen = _binary_background_7_frag_size;
	gl_clear_error();
	ret = gl_shader_new(&gt->background_shader[6],
						vert, vlen,
						frag, flen,
						background_attr, 1,
						log_llog, NULL);
	if (ret)
		goto err_shader;

	gt->uni_res_background[6] = gl_shader_get_uniform(gt->background_shader[6], "resolution");
	gt->uni_time_background[6] = gl_shader_get_uniform(gt->background_shader[6], "time");
	gt->uni_proj_background[6] = gl_shader_get_uniform(gt->background_shader[6], "projection");

	if (gl_has_error(gt->background_shader[6])) {
		log_warning("cannot create background shader 7");
		goto err_background_shader;
	}
	/** background end *********/

	mode = uterm_display_get_current(txt->disp);
	gt->sw = uterm_mode_get_width(mode);
	gt->sh = uterm_mode_get_height(mode);

	if (txt->orientation == ORIENTATION_NORMAL || txt->orientation == ORIENTATION_INVERTED) {
		txt->cols = gt->sw / FONT_WIDTH(txt);
		txt->rows = gt->sh / FONT_HEIGHT(txt);
	} else if (txt->orientation == ORIENTATION_RIGHT || txt->orientation == ORIENTATION_LEFT) {
		txt->cols = gt->sh / FONT_WIDTH(txt);
		txt->rows = gt->sw / FONT_HEIGHT(txt);
	}

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &s);
	if (s <= 0)
		s = 64;
	else if (s > 2048)
		s = 2048;
	gt->max_tex_size = s;

	gl_clear_error();

	ext = (const char*)glGetString(GL_EXTENSIONS);
	if (ext && strstr((const char*)ext, "GL_EXT_unpack_subimage")) {
		gt->supports_rowlen = true;
	} else {
		log_warning("your GL implementation does not support GL_EXT_unpack_subimage, glyph-rendering may be slower than usual");
	}

	return 0;

err_background_shader:
	gl_shader_unref(gt->background_shader[0]);
	gl_shader_unref(gt->background_shader[1]);
	gl_shader_unref(gt->background_shader[2]);
	gl_shader_unref(gt->background_shader[3]);
	gl_shader_unref(gt->background_shader[4]);
	gl_shader_unref(gt->background_shader[5]);
	gl_shader_unref(gt->background_shader[6]);
err_mouse_pointer_shader:
	gl_shader_unref(gt->mouse_pointer_shader);
err_shader:
	gl_shader_unref(gt->shader);
err_bold_htable:
	shl_hashtable_free(gt->bold_glyphs);
err_htable:
	shl_hashtable_free(gt->glyphs);
	return ret;
}

static void gltex_unset(struct kmscon_text *txt)
{
	struct gltex *gt = txt->data;
	int ret;
	struct shl_dlist *iter;
	struct atlas *atlas;
	bool gl = true;

	ret = uterm_display_use(txt->disp, NULL);
	if (ret) {
		gl = false;
		log_warning("cannot activate OpenGL-CTX during destruction");
	}

	shl_hashtable_free(gt->bold_glyphs);
	shl_hashtable_free(gt->glyphs);

	while (!shl_dlist_empty(&gt->atlases)) {
		iter = gt->atlases.next;
		shl_dlist_unlink(iter);
		atlas = shl_dlist_entry(iter, struct atlas, list);

		free(atlas->cache_pos);
		free(atlas->cache_texpos);
		free(atlas->cache_fgcol);
		free(atlas->cache_bgcol);

		if (gl)
			gl_tex_free(&atlas->tex, 1);
		free(atlas);
	}

	if (gl) {
		gl_shader_unref(gt->shader);

		gl_clear_error();
	}
}

/* returns an atlas with at least 1 free glyph position; NULL on error */
static struct atlas *get_atlas(struct kmscon_text *txt, unsigned int num)
{
	struct gltex *gt = txt->data;
	struct atlas *atlas;
	size_t newsize;
	unsigned int width, height, nsize;
	GLenum err;

	/* check whether the last added atlas has still room for one glyph */
	if (!shl_dlist_empty(&gt->atlases)) {
		atlas = shl_dlist_entry(gt->atlases.next, struct atlas,
					   list);
		if (atlas->fill + num <= atlas->count)
			return atlas;
	}

	/* all atlases are full so we have to create a new atlas */
	atlas = malloc(sizeof(*atlas));
	if (!atlas)
		return NULL;
	memset(atlas, 0, sizeof(*atlas));

	gl_clear_error();

	gl_tex_new(&atlas->tex, 1);
	err = glGetError();
	if (err != GL_NO_ERROR || !atlas->tex) {
		gl_clear_error();
		log_warning("cannot create new OpenGL texture: %d", err);
		goto err_free;
	}

	newsize = gt->max_tex_size / FONT_WIDTH(txt);
	if (newsize < 1)
		newsize = 1;

	/* OpenGL texture sizes are heavily restricted so we need to find a
	 * valid texture size that is big enough to hold as many glyphs as
	 * possible but at least 1 */
try_next:
	width = shl_next_pow2(FONT_WIDTH(txt) * newsize);
	height = shl_next_pow2(FONT_HEIGHT(txt));

	gl_clear_error();

	glBindTexture(GL_TEXTURE_2D, atlas->tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height,
		     0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);

	err = glGetError();
	if (err != GL_NO_ERROR) {
		if (newsize > 1) {
			--newsize;
			goto try_next;
		}
		gl_clear_error();
		log_warning("OpenGL textures too small for a single glyph (%d)",
			    err);
		goto err_tex;
	}

	log_debug("new atlas of size %ux%u for %zu", width, height, newsize);

	nsize = txt->cols * txt->rows;

	atlas->cache_pos = malloc(sizeof(GLfloat) * nsize * 2 * 6);
	if (!atlas->cache_pos)
		goto err_mem;

	atlas->cache_texpos = malloc(sizeof(GLfloat) * nsize * 2 * 6);
	if (!atlas->cache_texpos)
		goto err_mem;

	atlas->cache_fgcol = malloc(sizeof(GLfloat) * nsize * 3 * 6);
	if (!atlas->cache_fgcol)
		goto err_mem;

	atlas->cache_bgcol = malloc(sizeof(GLfloat) * nsize * 3 * 6);
	if (!atlas->cache_bgcol)
		goto err_mem;

	atlas->cache_size = nsize;
	atlas->count = newsize;
	atlas->width = width;
	atlas->height = height;
	atlas->advance_htex = 1.0 / atlas->width * FONT_WIDTH(txt);
	atlas->advance_vtex = 1.0 / atlas->height * FONT_HEIGHT(txt);

	shl_dlist_link(&gt->atlases, &atlas->list);
	return atlas;

err_mem:
	free(atlas->cache_pos);
	free(atlas->cache_texpos);
	free(atlas->cache_fgcol);
	free(atlas->cache_bgcol);
err_tex:
	gl_tex_free(&atlas->tex, 1);
err_free:
	free(atlas);
	return NULL;
}

static int find_glyph(struct kmscon_text *txt, struct glyph **out,
		      uint64_t id, const uint32_t *ch, size_t len, const struct tsm_screen_attr *attr)
{
	struct gltex *gt = txt->data;
	struct atlas *atlas;
	struct glyph *glyph;
	bool res;
	int ret, i;
	GLenum err;
	uint8_t *packed_data, *dst, *src;
	struct shl_hashtable *gtable;
	struct kmscon_font *font;

	if (attr->bold) {
		gtable = gt->bold_glyphs;
		font = txt->bold_font;
	} else {
		gtable = gt->glyphs;
		font = txt->font;
	}

	if (attr->underline)
		font->attr.underline = true;
	else
		font->attr.underline = false;

	if (attr->italic)
		font->attr.italic = true;
	else
		font->attr.italic = false;

	res = shl_hashtable_find(gtable, (void**)&glyph, id);
	if (res) {
		*out = glyph;
		return 0;
	}

	glyph = malloc(sizeof(*glyph));
	if (!glyph)
		return -ENOMEM;
	memset(glyph, 0, sizeof(*glyph));

	if (!len)
		ret = kmscon_font_render_empty(font, &glyph->glyph);
	else
		ret = kmscon_font_render(font, id, ch, len, &glyph->glyph);

	if (ret) {
		ret = kmscon_font_render_inval(font, &glyph->glyph);
		if (ret)
			goto err_free;
	}

	atlas = get_atlas(txt, glyph->glyph->width);
	if (!atlas) {
		ret = -EFAULT;
		goto err_free;
	}

	/* Funnily, not all OpenGLESv2 implementations support specifying the
	 * stride of a texture. Therefore, we then need to create a
	 * temporary image with a stride equal to the image width for loading
	 * the texture. This may slow down loading new glyphs but doesn't affect
	 * overall rendering performance. But driver developers should really
	 * add this! */

	gl_clear_error();

	glBindTexture(GL_TEXTURE_2D, atlas->tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if (!gt->supports_rowlen) {
		if (GLYPH_STRIDE(glyph) == GLYPH_WIDTH(glyph)) {
			glTexSubImage2D(GL_TEXTURE_2D, 0,
					FONT_WIDTH(txt) * atlas->fill, 0,
					GLYPH_WIDTH(glyph),
					GLYPH_HEIGHT(glyph),
					GL_ALPHA, GL_UNSIGNED_BYTE,
					GLYPH_DATA(glyph));
		} else {
			packed_data = malloc(GLYPH_WIDTH(glyph) * GLYPH_HEIGHT(glyph));
			if (!packed_data) {
				log_error("cannot allocate memory for glyph storage");
				ret = -ENOMEM;
				goto err_free;
			}

			src = GLYPH_DATA(glyph);
			dst = packed_data;
			for (i = 0; i < GLYPH_HEIGHT(glyph); ++i) {
				memcpy(dst, src, GLYPH_WIDTH(glyph));
				dst += GLYPH_WIDTH(glyph);
				src += GLYPH_STRIDE(glyph);
			}

			glTexSubImage2D(GL_TEXTURE_2D, 0,
					FONT_WIDTH(txt) * atlas->fill, 0,
					GLYPH_WIDTH(glyph),
					GLYPH_HEIGHT(glyph),
					GL_ALPHA, GL_UNSIGNED_BYTE,
					packed_data);
			free(packed_data);
		}
	} else {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, GLYPH_STRIDE(glyph));
		glTexSubImage2D(GL_TEXTURE_2D, 0,
				FONT_WIDTH(txt) * atlas->fill, 0,
				GLYPH_WIDTH(glyph),
				GLYPH_HEIGHT(glyph),
				GL_ALPHA, GL_UNSIGNED_BYTE,
				GLYPH_DATA(glyph));
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	/* Check for GL-errors
	 * As OpenGL is a state-machine, we cannot really tell which call failed
	 * without adding a glGetError() after each call. This is totally
	 * overkill so let us at least catch the error afterwards.
	 * We also add a hint to disable OpenGL if this does not work. This
	 * should _always_ work but OpenGL is kind of a black-box that isn't
	 * verbose at all and many things can go wrong. */

	err = glGetError();
	if (err != GL_NO_ERROR) {
		gl_clear_error();
		log_warning("cannot load glyph data into OpenGL texture (%d: %s); disable the GL-renderer if this does not work reliably",
			    err, gl_err_to_str(err));
		ret = -EFAULT;
		goto err_free;
	}

	glyph->atlas = atlas;
	glyph->texoff = atlas->fill;

	ret = shl_hashtable_insert(gtable, id, glyph);
	if (ret)
		goto err_free;

	atlas->fill += glyph->glyph->width;

	*out = glyph;
	return 0;

err_free:
	free(glyph);
	return ret;
}

static int gltex_rotate(struct kmscon_text *txt, unsigned int orientation)
{
	struct gltex *gt = txt->data;
	int ret = 0;

	txt->orientation = orientation;

	if (txt->orientation == ORIENTATION_NORMAL || txt->orientation == ORIENTATION_INVERTED) {
		txt->cols = gt->sw / FONT_WIDTH(txt);
		txt->rows = gt->sh / FONT_HEIGHT(txt);
	} else if (txt->orientation == ORIENTATION_RIGHT || txt->orientation == ORIENTATION_LEFT) {
		txt->cols = gt->sh / FONT_WIDTH(txt);
		txt->rows = gt->sw / FONT_HEIGHT(txt);
	}

	if (txt->orientation == ORIENTATION_NORMAL ) {
		gt->angle = .0;
	} else if (txt->orientation == ORIENTATION_INVERTED) {
		gt->angle = 180.;
	} else if (txt->orientation == ORIENTATION_RIGHT) {
		gt->angle = 90.;
	} else if (txt->orientation == ORIENTATION_LEFT) {
		gt->angle = -90.;
	}

	if (txt->orientation == ORIENTATION_NORMAL || txt->orientation == ORIENTATION_INVERTED) {
		gt->advance_x = 2.0 / gt->sw * FONT_WIDTH(txt);
		gt->advance_y = 2.0 / gt->sh * FONT_HEIGHT(txt);
	} else if (txt->orientation == ORIENTATION_RIGHT || txt->orientation == ORIENTATION_LEFT) {
		float aspect = (float) gt->sw / (float) gt->sh;
		gt->advance_x = 2.0 / gt->sw * FONT_WIDTH(txt) * aspect;
		gt->advance_y = 2.0 / gt->sh * FONT_HEIGHT(txt) * (1./aspect);
	}

	return ret;
}

static int gltex_prepare(struct kmscon_text *txt)
{
	struct gltex *gt = txt->data;
	struct atlas *atlas;
	struct shl_dlist *iter;
	int ret;

	ret = uterm_display_use(txt->disp, NULL);
	if (ret)
		return ret;

	shl_dlist_for_each(iter, &gt->atlases) {
		atlas = shl_dlist_entry(iter, struct atlas, list);

		atlas->cache_num = 0;
	}

	if (txt->orientation == ORIENTATION_NORMAL ) {
		gt->angle = .0;
	} else if (txt->orientation == ORIENTATION_INVERTED) {
		gt->angle = 180.;
	} else if (txt->orientation == ORIENTATION_RIGHT) {
		gt->angle = 90.;
	} else if (txt->orientation == ORIENTATION_LEFT) {
		gt->angle = -90.;
	}

	if (txt->orientation == ORIENTATION_NORMAL || txt->orientation == ORIENTATION_INVERTED) {
		gt->advance_x = 2.0 / gt->sw * FONT_WIDTH(txt);
		gt->advance_y = 2.0 / gt->sh * FONT_HEIGHT(txt);
	} else if (txt->orientation == ORIENTATION_RIGHT || txt->orientation == ORIENTATION_LEFT) {
		float aspect = (float) gt->sw / (float) gt->sh;
		gt->advance_x = 2.0 / gt->sw * FONT_WIDTH(txt) * aspect;
		gt->advance_y = 2.0 / gt->sh * FONT_HEIGHT(txt) * (1./aspect);
	}

	return 0;
}

static int gltex_draw(struct kmscon_text *txt,
		      uint64_t id, const uint32_t *ch, size_t len,
		      unsigned int width,
		      unsigned int posx, unsigned int posy,
		      const struct tsm_screen_attr *attr)
{
	struct gltex *gt = txt->data;
	struct atlas *atlas;
	struct glyph *glyph;
	int ret, i, idx;

	if (!width)
		return 0;

	ret = find_glyph(txt, &glyph, id, ch, len, attr);
	if (ret)
		return ret;
	atlas = glyph->atlas;

	if (atlas->cache_num >= atlas->cache_size)
		return -ERANGE;

	atlas->cache_pos[atlas->cache_num * 2 * 6 + 0] =
		gt->advance_x * posx - 1;
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 1] =
		1 - gt->advance_y * posy;
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 2] =
		gt->advance_x * posx - 1;
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 3] =
		1 - (gt->advance_y * posy + gt->advance_y);
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 4] =
		gt->advance_x * posx + width * gt->advance_x - 1;
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 5] =
		1 - (gt->advance_y * posy + gt->advance_y);

	atlas->cache_pos[atlas->cache_num * 2 * 6 + 6] =
		gt->advance_x * posx - 1;
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 7] =
		1 - gt->advance_y * posy;
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 8] =
		gt->advance_x * posx + width * gt->advance_x - 1;
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 9] =
		1 - (gt->advance_y * posy + gt->advance_y);
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 10] =
		gt->advance_x * posx + width * gt->advance_x - 1;
	atlas->cache_pos[atlas->cache_num * 2 * 6 + 11] =
		1 - gt->advance_y * posy;

	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 0] = glyph->texoff;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 1] = 0.0;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 2] = glyph->texoff;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 3] = 1.0;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 4] = glyph->texoff + width;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 5] = 1.0;

	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 6] = glyph->texoff;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 7] = 0.0;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 8] = glyph->texoff + width;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 9] = 1.0;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 10] = glyph->texoff + width;
	atlas->cache_texpos[atlas->cache_num * 2 * 6 + 11] = 0.0;

	for (i = 0; i < 6; ++i) {
		idx = atlas->cache_num * 3 * 6 + i * 3;
		if (attr->inverse) {
			atlas->cache_fgcol[idx + 0] = attr->br / 255.0;
			atlas->cache_fgcol[idx + 1] = attr->bg / 255.0;
			atlas->cache_fgcol[idx + 2] = attr->bb / 255.0;
			atlas->cache_bgcol[idx + 0] = attr->fr / 255.0;
			atlas->cache_bgcol[idx + 1] = attr->fg / 255.0;
			atlas->cache_bgcol[idx + 2] = attr->fb / 255.0;
		} else {
			atlas->cache_fgcol[idx + 0] = attr->fr / 255.0;
			atlas->cache_fgcol[idx + 1] = attr->fg / 255.0;
			atlas->cache_fgcol[idx + 2] = attr->fb / 255.0;
			atlas->cache_bgcol[idx + 0] = attr->br / 255.0;
			atlas->cache_bgcol[idx + 1] = attr->bg / 255.0;
			atlas->cache_bgcol[idx + 2] = attr->bb / 255.0;
		}
	}

	++atlas->cache_num;

	return 0;
}

static float fake_time = .0f;

int render_background (struct kmscon_text* txt)
{
	struct gltex *gt = txt->data;
	float mat[16];
	int shader_index = txt->shader_index;

	gl_clear_error();

	gl_shader_use(gt->background_shader[shader_index]);

	glViewport(0, 0, gt->sw, gt->sh);
	glDisable(GL_BLEND);

	gl_m4_identity(mat);
	glUniformMatrix4fv(gt->uni_proj_background[shader_index], 1, GL_FALSE, mat);

	glUniform2f(gt->uni_res_background[shader_index], (GLfloat) gt->sw, (GLfloat) gt->sh);

	fake_time += 0.01f;
	glUniform1f(gt->uni_time_background[shader_index], fake_time);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	// background
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, background_vertices);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, background_tex_coords);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, background_indices);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	return 0;
}

static int gltex_render(struct kmscon_text *txt)
{
	struct gltex *gt = txt->data;
	struct atlas *atlas;
	struct shl_dlist *iter;
	float mat[16];

	render_background(txt);

	gl_clear_error();

	gl_shader_use(gt->shader);

	glViewport(0, 0, gt->sw, gt->sh);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl_m4_identity(mat);
	glUniformMatrix4fv(gt->uni_proj, 1, GL_FALSE, mat);
	glUniform1f(gt->uni_orientation, gt->angle);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);

	glActiveTexture(GL_TEXTURE0);
	glUniform1i(gt->uni_atlas, 0);

	shl_dlist_for_each(iter, &gt->atlases) {
		atlas = shl_dlist_entry(iter, struct atlas, list);
		if (!atlas->cache_num)
			continue;

		glBindTexture(GL_TEXTURE_2D, atlas->tex);
		glUniform1f(gt->uni_advance_htex, atlas->advance_htex);
		glUniform1f(gt->uni_advance_vtex, atlas->advance_vtex);

		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, atlas->cache_pos);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, atlas->cache_texpos);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, atlas->cache_fgcol);
		glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 0, atlas->cache_bgcol);
		glDrawArrays(GL_TRIANGLES, 0, 6 * atlas->cache_num);
	}

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);

	if (gl_has_error(gt->shader)) {
		log_warning("rendering console caused OpenGL errors");
		return -EFAULT;
	}

	return 0;
}

static int gltex_render_pointer(struct kmscon_text *txt, int cursor_x, int cursor_y)
{
	struct gltex *gt = txt->data;
	float mat[16];

	gl_clear_error();

	gl_shader_use(gt->mouse_pointer_shader);

	GLfloat pixel_w = .0f;
	GLfloat pixel_h = .0f;
	GLfloat hw = .0f;
	GLfloat hh = .0f;

	if (txt->orientation == ORIENTATION_NORMAL || txt->orientation == ORIENTATION_INVERTED) {
		pixel_w = 2.0f/gt->sw;
		pixel_h = 2.0f/gt->sh;
		hw = FONT_WIDTH(txt)*pixel_w*.5f;
		hh = FONT_HEIGHT(txt)*pixel_h*.5f;
	} else if (txt->orientation == ORIENTATION_RIGHT || txt->orientation == ORIENTATION_LEFT) {
		pixel_w = 2.0f/gt->sh;
		pixel_h = 2.0f/gt->sw;
		hw = FONT_WIDTH(txt)*pixel_w*.5f;
		hh = FONT_HEIGHT(txt)*pixel_h*.5f;
	}

	mouse_block_vertices[0] = -hw; // top-left
	mouse_block_vertices[1] =  hh;
	mouse_block_vertices[2] =  hw - pixel_w; // top-right
	mouse_block_vertices[3] =  hh;
	mouse_block_vertices[4] = -hw; // bottom-left
	mouse_block_vertices[5] = -hh + pixel_h;
	mouse_block_vertices[6] =  hw - pixel_w; // bottom-right
	mouse_block_vertices[7] = -hh + pixel_h;

	glViewport(0, 0, gt->sw, gt->sh);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl_m4_identity(mat);
	glUniformMatrix4fv(gt->uni_proj_mouse, 1, GL_FALSE, mat);
	glUniform1f(gt->uni_orientation_mouse, gt->angle);

	GLfloat top_left[2] = {-1.f, 1.f};
	GLfloat x = top_left[0] + hw + cursor_x*FONT_WIDTH(txt)*pixel_w;
	GLfloat y = top_left[1] - hh - cursor_y*FONT_HEIGHT(txt)*pixel_h;
	glUniform2f(gt->uni_offset_mouse, x, y);

	glEnableVertexAttribArray(0);

	// block-cursor outline
	glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, mouse_block_vertices);
	glUniform4f(gt->uni_color_mouse, 1.f, .5f, .25f, .9f);
	glDrawElements(GL_LINES, 8, GL_UNSIGNED_INT, mouse_block_outline_indices);

	// block-cursor fill
	glUniform4f(gt->uni_color_mouse, 1.f, .5f, .25f, .35f);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, mouse_block_fill_indices);

	glDisableVertexAttribArray(0);

	return 0;
}

struct kmscon_text_ops kmscon_text_gltex_ops = {
	.name = "gltex",
	.owner = NULL,
	.init = gltex_init,
	.destroy = gltex_destroy,
	.set = gltex_set,
	.unset = gltex_unset,
	.rotate = gltex_rotate,
	.prepare = gltex_prepare,
	.draw = gltex_draw,
	.render = gltex_render,
	.render_pointer = gltex_render_pointer,
	.abort = NULL,
};
