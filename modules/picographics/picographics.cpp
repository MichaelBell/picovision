#include "drivers/dv_display/dv_display.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "common/pimoroni_common.hpp"

#include "micropython/modules/util.hpp"

using namespace pimoroni;

extern "C" {
#include "picographics.h"
#include "py/stream.h"
#include "py/reader.h"
#include "extmod/vfs.h"

const std::string_view mp_obj_to_string_r(const mp_obj_t &obj) {
    if(mp_obj_is_str_or_bytes(obj)) {
        GET_STR_DATA_LEN(obj, str, str_len);
        return std::string_view((const char*)str, str_len);
    }
    mp_raise_TypeError("can't convert object to str implicitly");
}

typedef struct _ModPicoGraphics_obj_t {
    mp_obj_base_t base;
    PicoGraphics *graphics;
    DVDisplay *display;
    void *spritedata;
    //mp_obj_t scanline_callback; // Not really feasible in MicroPython
} ModPicoGraphics_obj_t;

size_t get_required_buffer_size(PicoGraphicsPenType pen_type, uint width, uint height) {
    switch(pen_type) {
        case PEN_DV_RGB888:
            return PicoGraphics_PenDV_RGB888::buffer_size(width, height);
        case PEN_DV_RGB555:
            return PicoGraphics_PenDV_RGB555::buffer_size(width, height);
        case PEN_DV_P5:
            return PicoGraphics_PenDV_P5::buffer_size(width, height);
        default:
            return 0;
    }
}

mp_obj_t ModPicoGraphics_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    ModPicoGraphics_obj_t *self = nullptr;

    enum { ARG_display, ARG_pen_type, ARG_width, ARG_height };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_display, MP_ARG_INT | MP_ARG_REQUIRED },
        { MP_QSTR_pen_type, MP_ARG_INT, { .u_int = PEN_DV_RGB888 } },
        { MP_QSTR_width, MP_ARG_INT, { .u_int = 320 } },
        { MP_QSTR_height, MP_ARG_INT, { .u_int = 240 } }
    };

    // Parse args.
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    self = m_new_obj_with_finaliser(ModPicoGraphics_obj_t);
    self->base.type = &ModPicoGraphics_type;

    int width = args[ARG_width].u_int;
    int height = args[ARG_height].u_int;
    int pen_type = args[ARG_pen_type].u_int;

    // Create an instance of the graphics library and DV display driver
    switch((PicoGraphicsPenType)pen_type) {
        case PEN_DV_RGB888:
            self->display = m_new_class(DVDisplay, width, height, DVDisplay::MODE_RGB888);
            self->graphics = m_new_class(PicoGraphics_PenDV_RGB888, width, height, *(IDirectDisplayDriver<RGB888> *)self->display);
            break;
        case PEN_DV_RGB555:
            self->display = m_new_class(DVDisplay, width, height, DVDisplay::MODE_RGB555);
            self->graphics = m_new_class(PicoGraphics_PenDV_RGB555, width, height, *(IDirectDisplayDriver<uint16_t> *)self->display);
            break;
        case PEN_DV_P5:
            self->display = m_new_class(DVDisplay, width, height, DVDisplay::MODE_PALETTE);
            self->graphics = m_new_class(PicoGraphics_PenDV_P5, width, height, *(IPaletteDisplayDriver *)self->display);
            break;
        default:
            break;
    }

    self->display->init();

    self->spritedata = nullptr;

    // Clear each buffer
    for(auto x = 0u; x < 2u; x++){
        self->graphics->set_pen(0);
        self->graphics->clear();
        self->display->flip();
    }

    return MP_OBJ_FROM_PTR(self);
}

mp_obj_t ModPicoGraphics__del__(mp_obj_t self_in) {
    (void)self_in;
    return mp_const_none;
}

mp_obj_t ModPicoGraphics_set_spritesheet(mp_obj_t self_in, mp_obj_t spritedata) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);
    if(spritedata == mp_const_none) {
        self->spritedata = nullptr;
    } else {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(spritedata, &bufinfo, MP_BUFFER_RW);

        int required_size = get_required_buffer_size((PicoGraphicsPenType)self->graphics->pen_type, 128, 128);

        if(bufinfo.len != (size_t)(required_size)) {
            mp_raise_ValueError("Spritesheet the wrong size!");
        }

        self->spritedata = bufinfo.buf;
    }
    return mp_const_none;
}

mp_obj_t ModPicoGraphics_load_spritesheet(mp_obj_t self_in, mp_obj_t filename) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);
    mp_obj_t args[2] = {
        filename,
        MP_OBJ_NEW_QSTR(MP_QSTR_r),
    };

    // Stat the file to get its size
    // example tuple response: (32768, 0, 0, 0, 0, 0, 5153, 1654709815, 1654709815, 1654709815)
    mp_obj_t stat = mp_vfs_stat(filename);
    mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR2(stat, mp_obj_tuple_t);
    size_t filesize = mp_obj_get_int(tuple->items[6]);

    mp_buffer_info_t bufinfo;
    bufinfo.buf = (void *)m_new(uint8_t, filesize);
    mp_obj_t file = mp_vfs_open(MP_ARRAY_SIZE(args), &args[0], (mp_map_t *)&mp_const_empty_map);
    int errcode;
    bufinfo.len = mp_stream_rw(file, bufinfo.buf, filesize, &errcode, MP_STREAM_RW_READ | MP_STREAM_RW_ONCE);
    if (errcode != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to open sprite file!"));
    }

    self->spritedata = bufinfo.buf;

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_sprite(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_sprite_x, ARG_sprite_y, ARG_x, ARG_y, ARG_scale, ARG_transparent };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    if(self->spritedata == nullptr) return mp_const_false;

    int scale = 1;
    int transparent = 0;

    if(n_args >= 6) scale = mp_obj_get_int(args[ARG_scale]);
    if(n_args >= 7) transparent = mp_obj_get_int(args[ARG_transparent]);

    self->graphics->sprite(
        self->spritedata,
        {mp_obj_get_int(args[ARG_sprite_x]), mp_obj_get_int(args[ARG_sprite_y])},
        {mp_obj_get_int(args[ARG_x]), mp_obj_get_int(args[ARG_y])},
        scale,
        transparent
    );

    return mp_const_true;
}

mp_obj_t ModPicoGraphics_set_font(mp_obj_t self_in, mp_obj_t font) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);
    self->graphics->set_font(mp_obj_to_string_r(font));
    return mp_const_none;
}

mp_obj_t ModPicoGraphics_get_bounds(mp_obj_t self_in) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);
    mp_obj_t tuple[2] = {
        mp_obj_new_int(self->graphics->bounds.w),
        mp_obj_new_int(self->graphics->bounds.h)
    };
    return mp_obj_new_tuple(2, tuple);
}

mp_obj_t ModPicoGraphics_update(mp_obj_t self_in) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);
    self->display->flip();
    return mp_const_none;
}

mp_obj_t ModPicoGraphics_module_RGB332_to_RGB(mp_obj_t rgb332) {
    RGB c((RGB332)mp_obj_get_int(rgb332));
    mp_obj_t t[] = {
        mp_obj_new_int(c.r),
        mp_obj_new_int(c.g),
        mp_obj_new_int(c.b),
    };
    return mp_obj_new_tuple(3, t);
}

mp_obj_t ModPicoGraphics_module_RGB565_to_RGB(mp_obj_t rgb565) {
    RGB c((RGB565)mp_obj_get_int(rgb565));
    mp_obj_t t[] = {
        mp_obj_new_int(c.r),
        mp_obj_new_int(c.g),
        mp_obj_new_int(c.b),
    };
    return mp_obj_new_tuple(3, t);
}

mp_obj_t ModPicoGraphics_module_RGB_to_RGB332(mp_obj_t r, mp_obj_t g, mp_obj_t b) {
    return mp_obj_new_int(RGB(
        mp_obj_get_int(r),
        mp_obj_get_int(g),
        mp_obj_get_int(b)
    ).to_rgb332());
}

mp_obj_t ModPicoGraphics_module_RGB_to_RGB565(mp_obj_t r, mp_obj_t g, mp_obj_t b) {
    return mp_obj_new_int(RGB(
        mp_obj_get_int(r),
        mp_obj_get_int(g),
        mp_obj_get_int(b)
    ).to_rgb565());
}

mp_obj_t ModPicoGraphics_set_pen(mp_obj_t self_in, mp_obj_t pen) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);

    self->graphics->set_pen(mp_obj_get_int(pen));

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_reset_pen(mp_obj_t self_in, mp_obj_t pen) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);

    self->graphics->reset_pen(mp_obj_get_int(pen));

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_update_pen(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_i, ARG_r, ARG_g, ARG_b };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    self->graphics->update_pen(
        mp_obj_get_int(args[ARG_i]) & 0xff,
        mp_obj_get_int(args[ARG_r]) & 0xff,
        mp_obj_get_int(args[ARG_g]) & 0xff,
        mp_obj_get_int(args[ARG_b]) & 0xff
    );

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_create_pen(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_r, ARG_g, ARG_b };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    int result = self->graphics->create_pen(
        mp_obj_get_int(args[ARG_r]) & 0xff,
        mp_obj_get_int(args[ARG_g]) & 0xff,
        mp_obj_get_int(args[ARG_b]) & 0xff
    );

    if (result == -1) mp_raise_ValueError("create_pen failed. No matching colour or space in palette!");

    return mp_obj_new_int(result);
}

mp_obj_t ModPicoGraphics_create_pen_hsv(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_h, ARG_s, ARG_v };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);
    int result = self->graphics->create_pen_hsv(
        mp_obj_get_float(args[ARG_h]),
        mp_obj_get_float(args[ARG_s]),
        mp_obj_get_float(args[ARG_v])
    );

    if (result == -1) mp_raise_ValueError("create_pen failed. No matching colour or space in palette!");

    return mp_obj_new_int(result);
}

mp_obj_t ModPicoGraphics_set_thickness(mp_obj_t self_in, mp_obj_t pen) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);

    self->graphics->set_thickness(mp_obj_get_int(pen));

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_set_palette(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    size_t num_tuples = n_args - 1;
    const mp_obj_t *tuples = pos_args + 1;

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(pos_args[0], ModPicoGraphics_obj_t);

    // Check if there is only one argument, which might be a list
    if(n_args == 2) {
        if(mp_obj_is_type(pos_args[1], &mp_type_list)) {
            mp_obj_list_t *points = MP_OBJ_TO_PTR2(pos_args[1], mp_obj_list_t);

            if(points->len <= 0) mp_raise_ValueError("set_palette(): cannot provide an empty list");

            num_tuples = points->len;
            tuples = points->items;
        }
        else {
            mp_raise_TypeError("set_palette(): can't convert object to list");
        }
    }

    for(size_t i = 0; i < num_tuples; i++) {
        mp_obj_t obj = tuples[i];
        if(!mp_obj_is_type(obj, &mp_type_tuple)) mp_raise_ValueError("set_palette(): can't convert object to tuple");

        mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR2(obj, mp_obj_tuple_t);

        if(tuple->len != 3) mp_raise_ValueError("set_palette(): tuple must contain R, G, B values");

        self->graphics->update_pen(
            i,
            mp_obj_get_int(tuple->items[0]),
            mp_obj_get_int(tuple->items[1]),
            mp_obj_get_int(tuple->items[2])
        );
    }

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_set_clip(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_x, ARG_y, ARG_w, ARG_h };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    self->graphics->set_clip({
        mp_obj_get_int(args[ARG_x]),
        mp_obj_get_int(args[ARG_y]),
        mp_obj_get_int(args[ARG_w]),
        mp_obj_get_int(args[ARG_h])
    });

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_remove_clip(mp_obj_t self_in) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);

    self->graphics->remove_clip();

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_clear(mp_obj_t self_in) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);

    self->graphics->clear();

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_pixel(mp_obj_t self_in, mp_obj_t x, mp_obj_t y) {
    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(self_in, ModPicoGraphics_obj_t);

    self->graphics->pixel({
        mp_obj_get_int(x),
        mp_obj_get_int(y)
    });

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_pixel_span(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_x, ARG_y, ARG_l };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    self->graphics->pixel_span({
        mp_obj_get_int(args[ARG_x]),
        mp_obj_get_int(args[ARG_y])
    },  mp_obj_get_int(args[ARG_l]));

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_rectangle(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_x, ARG_y, ARG_w, ARG_h };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    self->graphics->rectangle({
        mp_obj_get_int(args[ARG_x]),
        mp_obj_get_int(args[ARG_y]),
        mp_obj_get_int(args[ARG_w]),
        mp_obj_get_int(args[ARG_h])
    });

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_circle(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_x, ARG_y, ARG_r };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    self->graphics->circle({
        mp_obj_get_int(args[ARG_x]),
        mp_obj_get_int(args[ARG_y])
    },  mp_obj_get_int(args[ARG_r]));

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_character(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_self, ARG_char, ARG_x, ARG_y, ARG_scale };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_char, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_y, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_scale, MP_ARG_INT, {.u_int = 2} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self].u_obj, ModPicoGraphics_obj_t);

    int c = mp_obj_get_int(args[ARG_char].u_obj);
    int x = args[ARG_x].u_int;
    int y = args[ARG_y].u_int;
    int scale = args[ARG_scale].u_int;

    self->graphics->character((char)c, Point(x, y), scale);

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_text(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_self, ARG_text, ARG_x, ARG_y, ARG_wrap, ARG_scale, ARG_angle, ARG_spacing };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_text, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_x1, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_y1, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_wordwrap, MP_ARG_INT, {.u_int = __INT32_MAX__} },  // Sort-of a fudge, but wide-enough to avoid wrapping on any display size
        { MP_QSTR_scale, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_angle, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_spacing, MP_ARG_INT, {.u_int = 1} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self].u_obj, ModPicoGraphics_obj_t);

    mp_obj_t text_obj = args[ARG_text].u_obj;

    if(!mp_obj_is_str_or_bytes(text_obj)) mp_raise_TypeError("text: string required");

    GET_STR_DATA_LEN(text_obj, str, str_len);

    const std::string_view t((const char*)str, str_len);

    int x = args[ARG_x].u_int;
    int y = args[ARG_y].u_int;
    int wrap = args[ARG_wrap].u_int;
    float scale = args[ARG_scale].u_obj == mp_const_none ? 2.0f : mp_obj_get_float(args[ARG_scale].u_obj);
    int angle = args[ARG_angle].u_int;
    int letter_spacing = args[ARG_spacing].u_int;

    self->graphics->text(t, Point(x, y), wrap, scale, angle, letter_spacing);

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_measure_text(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_self, ARG_text, ARG_scale, ARG_spacing };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_text, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_scale, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_spacing, MP_ARG_INT, {.u_int = 1} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self].u_obj, ModPicoGraphics_obj_t);

    mp_obj_t text_obj = args[ARG_text].u_obj;

    if(!mp_obj_is_str_or_bytes(text_obj)) mp_raise_TypeError("text: string required");

    GET_STR_DATA_LEN(text_obj, str, str_len);

    const std::string_view t((const char*)str, str_len);

    float scale = args[ARG_scale].u_obj == mp_const_none ? 2.0f : mp_obj_get_float(args[ARG_scale].u_obj);
    int letter_spacing = args[ARG_spacing].u_int;

    int width = self->graphics->measure_text(t, scale, letter_spacing);

    return mp_obj_new_int(width);
}

mp_obj_t ModPicoGraphics_polygon(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    size_t num_tuples = n_args - 1;
    const mp_obj_t *tuples = pos_args + 1;

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(pos_args[0], ModPicoGraphics_obj_t);

    // Check if there is only one argument, which might be a list
    if(n_args == 2) {
        if(mp_obj_is_type(pos_args[1], &mp_type_list)) {
            mp_obj_list_t *points = MP_OBJ_TO_PTR2(pos_args[1], mp_obj_list_t);

            if(points->len <= 0) mp_raise_ValueError("poly(): cannot provide an empty list");

            num_tuples = points->len;
            tuples = points->items;
        }
        else {
            mp_raise_TypeError("poly(): can't convert object to list");
        }
    }

    if(num_tuples > 0) {
        std::vector<Point> points;
        for(size_t i = 0; i < num_tuples; i++) {
            mp_obj_t obj = tuples[i];
            if(!mp_obj_is_type(obj, &mp_type_tuple)) mp_raise_ValueError("poly(): can't convert object to tuple");

            mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR2(obj, mp_obj_tuple_t);

            if(tuple->len != 2) mp_raise_ValueError("poly(): tuple must only contain two numbers");

            points.push_back({
                mp_obj_get_int(tuple->items[0]),
                mp_obj_get_int(tuple->items[1])
            });
        }
        self->graphics->polygon(points);
    }

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_triangle(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_x1, ARG_y1, ARG_x2, ARG_y2, ARG_x3, ARG_y3 };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    self->graphics->triangle(
        {mp_obj_get_int(args[ARG_x1]),
         mp_obj_get_int(args[ARG_y1])},
        {mp_obj_get_int(args[ARG_x2]),
         mp_obj_get_int(args[ARG_y2])},
        {mp_obj_get_int(args[ARG_x3]),
         mp_obj_get_int(args[ARG_y3])}
    );

    return mp_const_none;
}

mp_obj_t ModPicoGraphics_line(size_t n_args, const mp_obj_t *args) {
    enum { ARG_self, ARG_x1, ARG_y1, ARG_x2, ARG_y2, ARG_thickness };

    ModPicoGraphics_obj_t *self = MP_OBJ_TO_PTR2(args[ARG_self], ModPicoGraphics_obj_t);

    if(n_args == 5) {
        self->graphics->line(
            {mp_obj_get_int(args[ARG_x1]),
            mp_obj_get_int(args[ARG_y1])},
            {mp_obj_get_int(args[ARG_x2]),
            mp_obj_get_int(args[ARG_y2])}
        );
    }
    else if(n_args == 6) {
        self->graphics->thick_line(
            {mp_obj_get_int(args[ARG_x1]),
            mp_obj_get_int(args[ARG_y1])},
            {mp_obj_get_int(args[ARG_x2]),
            mp_obj_get_int(args[ARG_y2])},
            mp_obj_get_int(args[ARG_thickness])
        );
    }

    return mp_const_none;
}
}
