#include "private.h"
#include "../include/glext.h"
#include <stdio.h>

#define CLAMP_U (1<<1)
#define CLAMP_V (1<<0)

static TextureObject* TEXTURE_UNITS[MAX_TEXTURE_UNITS] = {NULL, NULL};
static NamedArray TEXTURE_OBJECTS;
static GLubyte ACTIVE_TEXTURE = 0;

static GLuint _determinePVRFormat(GLint internalFormat, GLenum type);

GLubyte _glKosInitTextures() {
    named_array_init(&TEXTURE_OBJECTS, sizeof(TextureObject), 256);
    return 1;
}

TextureObject* getTexture0() {
    return TEXTURE_UNITS[0];
}

TextureObject* getTexture1() {
    return TEXTURE_UNITS[1];
}

TextureObject* getBoundTexture() {
    return TEXTURE_UNITS[ACTIVE_TEXTURE];
}

GLubyte check_valid_enum(GLint param, GLenum* values, const char* func) {
    GLubyte found = 0;
    while(*values != 0) {
        if(*values == param) {
            found++;
            break;
        }
        values++;
    }

    if(!found) {
        _glKosThrowError(GL_INVALID_ENUM, func);
        _glKosPrintError();
        return 1;
    }

    return 0;
}


void APIENTRY glActiveTextureARB(GLenum texture) {
    TRACE();

    if(texture < GL_TEXTURE0_ARB || texture > GL_TEXTURE0_ARB + MAX_TEXTURE_UNITS)
        _glKosThrowError(GL_INVALID_ENUM, "glActiveTextureARB");

    if(_glKosHasError()) {
        _glKosPrintError();
        return;
    }

    ACTIVE_TEXTURE = texture & 0xF;
}

GLboolean APIENTRY glIsTexture(GLuint texture) {
    return (named_array_used(&TEXTURE_OBJECTS, texture)) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glGenTextures(GLsizei n, GLuint *textures) {
    TRACE();

    while(n--) {
        GLuint id = 0;
        TextureObject* txr = (TextureObject*) named_array_alloc(&TEXTURE_OBJECTS, &id);
        txr->index = id;
        txr->width = txr->height = 0;
        txr->mip_map = 0;
        txr->uv_clamp = 0;
        txr->env = PVR_TXRENV_MODULATEALPHA;
        txr->filter = PVR_FILTER_NONE;
        txr->data = NULL;

        *textures = id;

        textures++;
    }
}

void APIENTRY glDeleteTextures(GLsizei n, GLuint *textures) {
    TRACE();

    while(n--) {
        TextureObject* txr = (TextureObject*) named_array_get(&TEXTURE_OBJECTS, *textures);

        /* Make sure we update framebuffer objects that have this texture attached */
        wipeTextureOnFramebuffers(*textures);

        if(txr == TEXTURE_UNITS[ACTIVE_TEXTURE]) {
            TEXTURE_UNITS[ACTIVE_TEXTURE] = NULL;
        }

        if(txr->data) {
            pvr_mem_free(txr->data);
        }

        named_array_release(&TEXTURE_OBJECTS, *textures++);
    }
}

void APIENTRY glBindTexture(GLenum  target, GLuint texture) {
    TRACE();

    GLenum target_values [] = {GL_TEXTURE_2D, 0};

    if(check_valid_enum(target, target_values, __func__) != 0) {
        return;
    }

    if(texture) {
        TEXTURE_UNITS[ACTIVE_TEXTURE] = (TextureObject*) named_array_get(&TEXTURE_OBJECTS, texture);
    } else {
        TEXTURE_UNITS[ACTIVE_TEXTURE] = NULL;
    }
}

void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param) {
    TRACE();

    GLenum target_values [] = {GL_TEXTURE_ENV, 0};
    GLenum pname_values [] = {GL_TEXTURE_ENV_MODE, 0};
    GLenum param_values [] = {GL_MODULATE, GL_DECAL, GL_REPLACE, 0};

    GLubyte failures = 0;

    failures += check_valid_enum(target, target_values, __func__);
    failures += check_valid_enum(pname, pname_values, __func__);
    failures += check_valid_enum(param, param_values, __func__);

    TextureObject* active = TEXTURE_UNITS[ACTIVE_TEXTURE];

    if(!active) {
        return;
    }

    if(failures) {
        return;
    }

    switch(param) {
    case GL_MODULATE:
        active->env = PVR_TXRENV_MODULATEALPHA;
    break;
    case GL_DECAL:
        active->env = PVR_TXRENV_DECAL;
    break;
    case GL_REPLACE:
        active->env = PVR_TXRENV_REPLACE;
    break;
    default:
        break;
    }
}

void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
    glTexEnvi(target, pname, param);
}

void APIENTRY glCompressedTexImage2D(GLenum target,
                                     GLint level,
                                     GLenum internalformat,
                                     GLsizei width,
                                     GLsizei height,
                                     GLint border,
                                     GLsizei imageSize,
                                     const GLvoid *data) {
    TRACE();

    if(target != GL_TEXTURE_2D)
        _glKosThrowError(GL_INVALID_ENUM, "glCompressedTexImage2D");

    if(level < 0)
        _glKosThrowError(GL_INVALID_VALUE, "glCompressedTexImage2D");

    if(border)
        _glKosThrowError(GL_INVALID_VALUE, "glCompressedTexImage2D");

    if(internalformat != GL_UNSIGNED_SHORT_5_6_5_VQ_KOS)
        if(internalformat != GL_UNSIGNED_SHORT_5_6_5_VQ_TWID_KOS)
            if(internalformat != GL_UNSIGNED_SHORT_4_4_4_4_VQ_KOS)
                if(internalformat != GL_UNSIGNED_SHORT_4_4_4_4_REV_VQ_TWID_KOS)
                    if(internalformat != GL_UNSIGNED_SHORT_1_5_5_5_REV_VQ_KOS)
                        if(internalformat != GL_UNSIGNED_SHORT_1_5_5_5_REV_VQ_TWID_KOS)
                            _glKosThrowError(GL_INVALID_OPERATION, "glCompressedTexImage2D");

    if(TEXTURE_UNITS[ACTIVE_TEXTURE] == NULL)
        _glKosThrowError(GL_INVALID_OPERATION, "glCompressedTexImage2D");

    if(_glKosHasError()) {
        _glKosPrintError();
        return;
    }

    TextureObject* active = TEXTURE_UNITS[ACTIVE_TEXTURE];

    active->width   = width;
    active->height  = height;
    active->mip_map = level;
    active->color   = _determinePVRFormat(
        internalformat,
        internalformat  /* Doesn't matter (see determinePVRFormat) */
    );

    /* Odds are slim new data is same size as old, so free always */
    if(active->data)
        pvr_mem_free(active->data);

    active->data = pvr_mem_malloc(imageSize);

    if(data)
        sq_cpy(active->data, data, imageSize);
}

static GLint _cleanInternalFormat(GLint internalFormat) {
    switch (internalFormat) {
    case GL_ALPHA:
/*    case GL_ALPHA4:
    case GL_ALPHA8:
    case GL_ALPHA12:
    case GL_ALPHA16:*/
       return GL_ALPHA;
    case 1:
    case GL_LUMINANCE:
/*    case GL_LUMINANCE4:
    case GL_LUMINANCE8:
    case GL_LUMINANCE12:
    case GL_LUMINANCE16:*/
       return GL_LUMINANCE;
    case 2:
    case GL_LUMINANCE_ALPHA:
/*    case GL_LUMINANCE4_ALPHA4:
    case GL_LUMINANCE6_ALPHA2:
    case GL_LUMINANCE8_ALPHA8:
    case GL_LUMINANCE12_ALPHA4:
    case GL_LUMINANCE12_ALPHA12:
    case GL_LUMINANCE16_ALPHA16: */
       return GL_LUMINANCE_ALPHA;
/*    case GL_INTENSITY:
    case GL_INTENSITY4:
    case GL_INTENSITY8:
    case GL_INTENSITY12:
    case GL_INTENSITY16:
       return GL_INTENSITY; */
    case 3:
       return GL_RGB;
    case GL_RGB:
/*    case GL_R3_G3_B2:
    case GL_RGB4:
    case GL_RGB5:
    case GL_RGB8:
    case GL_RGB10:
    case GL_RGB12:
    case GL_RGB16: */
       return GL_RGB;
    case 4:
       return GL_RGBA;
    case GL_RGBA:
/*    case GL_RGBA2:
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGBA8:
    case GL_RGB10_A2:
    case GL_RGBA12:
    case GL_RGBA16: */
        return GL_RGBA;

    /* Support ARB_texture_rg */
    case GL_RED:
/*    case GL_R8:
    case GL_R16:
    case GL_RED:
    case GL_COMPRESSED_RED: */
        return GL_RED;
/*    case GL_RG:
    case GL_RG8:
    case GL_RG16:
    case GL_COMPRESSED_RG:
        return GL_RG;*/
    default:
        return -1;
    }
}

static GLuint _determinePVRFormat(GLint internalFormat, GLenum type) {
    /* Given a cleaned internalFormat, return the Dreamcast format
     * that can hold it
     */
    switch(internalFormat) {
        case GL_ALPHA:
        case GL_LUMINANCE:
        case GL_LUMINANCE_ALPHA:
        case GL_RGBA:
        /* OK so if we have something that requires alpha, we return 4444 unless
         * the type was already 1555 (1-bit alpha) in which case we return that
         */
            return (type == GL_UNSIGNED_SHORT_1_5_5_5_REV) ?
                PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED :
                PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
        case GL_RED:
        case GL_RGB:
            /* No alpha? Return RGB565 which is the best we can do without using palettes */
            return PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
        /* Compressed and twiddled versions */
        case GL_UNSIGNED_SHORT_5_6_5_TWID_KOS:
            return PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED;
        case GL_UNSIGNED_SHORT_5_6_5_VQ_KOS:
            return PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_VQ_ENABLE;
        case GL_UNSIGNED_SHORT_5_6_5_VQ_TWID_KOS:
            return PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED | PVR_TXRFMT_VQ_ENABLE;
        case GL_UNSIGNED_SHORT_4_4_4_4_REV_TWID_KOS:
            return PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED;
        case GL_UNSIGNED_SHORT_4_4_4_4_REV_VQ_TWID_KOS:
            return PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED | PVR_TXRFMT_VQ_ENABLE;
        case GL_UNSIGNED_SHORT_4_4_4_4_VQ_KOS:
            return PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_VQ_ENABLE;
        case GL_UNSIGNED_SHORT_1_5_5_5_REV_TWID_KOS:
            return PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED;
        case GL_UNSIGNED_SHORT_1_5_5_5_REV_VQ_KOS:
            return PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_VQ_ENABLE;
        case GL_UNSIGNED_SHORT_1_5_5_5_REV_VQ_TWID_KOS:
            return PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED | PVR_TXRFMT_VQ_ENABLE;
        default:
            return 0;
    }
}


typedef void (*TextureConversionFunc)(const GLubyte*, GLushort*);

static void _rgba8888_to_argb4444(const GLubyte* source, GLushort* dest) {
    *dest = (source[3] & 0xF0) << 8 | (source[0] & 0xF0) << 4 | (source[1] & 0xF0) | (source[2] & 0xF0) >> 4;
}

static void _rgb888_to_rgb565(const GLubyte* source, GLushort* dest) {
    *dest = ((source[0] & 0b11111000) << 8) | ((source[1] & 0b11111100) << 3) | (source[2] >> 3);
}

static void _rgba8888_to_a000(const GLubyte* source, GLushort* dest) {
    *dest = ((source[3] & 0b11111000) << 8);
}

static void _r8_to_rgb565(const GLubyte* source, GLushort* dest) {
    *dest = (source[0] & 0b11111000) << 8;
}

static void _rgba4444_to_argb4444(const GLubyte* source, GLushort* dest) {
    GLushort* src = (GLushort*) source;
    *dest = ((*src & 0x000F) << 12) | *src >> 4;
}

static TextureConversionFunc _determineConversion(GLint internalFormat, GLenum format, GLenum type) {
    switch(internalFormat) {
    case GL_ALPHA: {
        if(type == GL_UNSIGNED_BYTE && format == GL_RGBA) {
            return _rgba8888_to_a000;
        } else if(type == GL_BYTE && format == GL_RGBA) {
            return _rgba8888_to_a000;
        }
    } break;
    case GL_RGB: {
        if(type == GL_UNSIGNED_BYTE && format == GL_RGB) {
            return _rgb888_to_rgb565;
        } else if(type == GL_BYTE && format == GL_RGB) {
            return _rgb888_to_rgb565;
        } else if(type == GL_UNSIGNED_BYTE && format == GL_RED) {
            return _r8_to_rgb565;
        }
    } break;
    case GL_RGBA: {
        if(type == GL_UNSIGNED_BYTE && format == GL_RGBA) {
            return _rgba8888_to_argb4444;
        } else if (type == GL_BYTE && format == GL_RGBA) {
            return _rgba8888_to_argb4444;
        } else if(type == GL_UNSIGNED_SHORT_4_4_4_4 && format == GL_RGBA) {
            return _rgba4444_to_argb4444;
        }
    } break;
    default:
        fprintf(stderr, "Unsupported conversion: %d -> %d, %d", internalFormat, format, type);
        break;
    }
    return 0;
}

static GLint _determineStride(GLenum format, GLenum type) {
    switch(type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return (format == GL_RED) ? 1 : (format == GL_RGB) ? 3 : 4;
    case GL_UNSIGNED_SHORT:
        return (format == GL_RED) ? 2 : (format == GL_RGB) ? 6 : 8;
    case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_5_6_5_TWID_KOS:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV_TWID_KOS:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV_TWID_KOS:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
        return 2;
    }

    return -1;
}

static GLboolean _isSupportedFormat(GLenum format) {
    switch(format) {
    case GL_RED:
    case GL_RGB:
    case GL_RGBA:
    case GL_BGRA:
        return GL_TRUE;
    default:
        return GL_FALSE;
    }
}


void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalFormat,
                           GLsizei width, GLsizei height, GLint border,
                           GLenum format, GLenum type, const GLvoid *data) {

    TRACE();

    if(target != GL_TEXTURE_2D) {
        _glKosThrowError(GL_INVALID_ENUM, "glTexImage2D");
    }

    if(!_isSupportedFormat(format)) {
        _glKosThrowError(GL_INVALID_ENUM, "glTexImage2D");
    }

    /* Abuse determineStride to see if type is valid */
    if(_determineStride(GL_RGBA, type) == -1) {
        _glKosThrowError(GL_INVALID_ENUM, "glTexImage2D");
    }

    internalFormat = _cleanInternalFormat(internalFormat);
    if(internalFormat == -1) {
        _glKosThrowError(GL_INVALID_VALUE, "glTexImage2D");
    }

    GLint w = width;
    if(w == 0 || (w & -w) != w) {
        /* Width is not a power of two. Must be!*/
        _glKosThrowError(GL_INVALID_VALUE, "glTexImage2D");
    }

    GLint h = height;
    if(h == 0 || (h & -h) != h) {
        /* height is not a power of two. Must be!*/
        _glKosThrowError(GL_INVALID_VALUE, "glTexImage2D");
    }

    /* FIXME: Mipmaps! */
    if(level < 0 || level > 0) {
        _glKosThrowError(GL_INVALID_VALUE, "glTexImage2D");
    }

    if(border) {
        _glKosThrowError(GL_INVALID_VALUE, "glTexImage2D");
    }

    if(!TEXTURE_UNITS[ACTIVE_TEXTURE]) {
        _glKosThrowError(GL_INVALID_OPERATION, "glTexImage2D");
    }

    if(_glKosHasError()) {
        _glKosPrintError();
        return;
    }

    /* Calculate the format that we need to convert the data to */
    GLuint pvr_format = _determinePVRFormat(internalFormat, type);

    TextureObject* active = TEXTURE_UNITS[ACTIVE_TEXTURE];

    if(active->data) {
        /* pre-existing texture - check if changed */
        if(active->width != width ||
           active->height != height ||
           active->mip_map != level ||
           active->color != pvr_format) {
            /* changed - free old texture memory */
            pvr_mem_free(active->data);
            active->data = NULL;
        }
    }    

    GLuint bytes = (width * height * sizeof(GLushort));

    if(!active->data) {
        /* need texture memory */
        active->width   = width;
        active->height  = height;
        active->mip_map = level;
        active->color   = pvr_format;
        active->data = pvr_mem_malloc(bytes);
    }

    /* Let's assume we need to convert */
    GLboolean needsConversion = GL_TRUE;

    /*
     * These are the only formats where the source format passed in matches the pvr format.
     * Note the REV formats + GL_BGRA will reverse to ARGB which is what the PVR supports
     */
    if(format == GL_BGRA && type == GL_UNSIGNED_SHORT_4_4_4_4_REV && internalFormat == GL_RGBA) {
        needsConversion = GL_FALSE;
    } else if(format == GL_BGRA && type == GL_UNSIGNED_SHORT_1_5_5_5_REV && internalFormat == GL_RGBA) {
        needsConversion = GL_FALSE;
    } else if(format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5 && internalFormat == GL_RGB) {
        needsConversion = GL_FALSE;
    } else if(format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5_TWID_KOS && internalFormat == GL_RGB) {
        needsConversion = GL_FALSE;
    } else if(format == GL_BGRA && type == GL_UNSIGNED_SHORT_1_5_5_5_REV_TWID_KOS && internalFormat == GL_RGBA) {
        needsConversion = GL_FALSE;
    } else if(format == GL_BGRA && type == GL_UNSIGNED_SHORT_4_4_4_4_REV_TWID_KOS && internalFormat == GL_RGBA) {
        needsConversion = GL_FALSE;
    }

    if(!data) {
        /* No data? Do nothing! */
        return;
    } else if(!needsConversion) {
        /* No conversion? Just copy the data, and the pvr_format is correct */
        sq_cpy(active->data, data, bytes);
        return;
    } else {
        TextureConversionFunc convert = _determineConversion(
            internalFormat,
            format,
            type
        );

        if(!convert) {
            _glKosThrowError(GL_INVALID_OPERATION, "glTexImage2D");
            return;
        }

        GLushort* dest = active->data;
        const GLubyte* source = data;
        GLint stride = _determineStride(format, type);

        if(stride == -1) {
            _glKosThrowError(GL_INVALID_OPERATION, "glTexImage2D");
            return;
        }

        /* Perform the conversion */
        GLuint i;
        for(i = 0; i < bytes; i += 2) {
            convert(source, dest);

            dest++;
            source += stride;
        }
    }
}

void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) {
    TRACE();

    TextureObject* active = getBoundTexture();

    if(!active) {
        return;
    }

    if(target == GL_TEXTURE_2D) {
        switch(pname) {
            case GL_TEXTURE_MAG_FILTER:
            case GL_TEXTURE_MIN_FILTER:
                switch(param) {
                    case GL_LINEAR:
                        active->filter = PVR_FILTER_BILINEAR;
                        break;

                    case GL_NEAREST:
                        active->filter = PVR_FILTER_NEAREST;
                        break;

                    case GL_FILTER_NONE:
                        active->filter = PVR_FILTER_NONE;
                        break;

                    case GL_FILTER_BILINEAR:
                        active->filter = PVR_FILTER_BILINEAR;
                        break;

                    default:
                        break;
                }

                break;

            case GL_TEXTURE_WRAP_S:
                switch(param) {
                    case GL_CLAMP:
                        active->uv_clamp |= CLAMP_U;
                        break;

                    case GL_REPEAT:
                        active->uv_clamp &= ~CLAMP_U;
                        break;
                }

                break;

            case GL_TEXTURE_WRAP_T:
                switch(param) {
                    case GL_CLAMP:
                        active->uv_clamp |= CLAMP_V;
                        break;

                    case GL_REPEAT:
                        active->uv_clamp &= ~CLAMP_V;
                        break;
                }

                break;
        }
    }
}
