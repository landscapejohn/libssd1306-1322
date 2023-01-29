#include "ssd1322_graphics.h"

int main()
{
    //ssd1322_6800_t *oled;
    //oled = ssd1306_i2c_open(256, 64, NULL);

    int rc = 0;
    ssd1322_err_t *errp = NULL;
    ssd1322_framebuffer_t *fbp = NULL;
    do {
        errp = ssd1322_err_create(stderr);
        if (!errp) {
            rc = -1;
            break;
        }

        fprintf(errp->err_fp, "DEBUG: Using library version: %s\n", ssd1322_fb_version());

        fbp = ssd1322_framebuffer_create(256, 64, errp);
        if (!fbp) {
            rc = -1;
            break;
        }

        fprintf(errp->err_fp, "DEBUG: draw horizontal line from (0,0) to (64,0)\n");
        ssd1322_framebuffer_draw_line(fbp, 0, 0, 64, 0, true);
        ssd1322_framebuffer_bitdump_nospace(fbp);
        ssd1322_framebuffer_clear(fbp);
        fprintf(errp->err_fp, "DEBUG: draw vertical line from (64,0) to (64,32)\n");
        ssd1322_framebuffer_draw_line(fbp, 64, 0, 64, 32, true);
        ssd1322_framebuffer_bitdump_nospace(fbp);
        ssd1322_framebuffer_clear(fbp);
        fprintf(errp->err_fp, "DEBUG: draw vertical line from (0,0) to (64,32)\n");
        ssd1322_framebuffer_draw_line(fbp, 0, 0, 64, 32, true);
        ssd1322_framebuffer_bitdump_nospace(fbp);
        ssd1322_framebuffer_clear(fbp);
        fprintf(errp->err_fp, "DEBUG: draw text\n");
        char buf[16] = { 0 };
        snprintf(buf, sizeof(buf) - 1, "%02d:%02d.%02d", 1, 58, 15);
        printf("INFO: Time is %s\n", buf);
        ssd1322_framebuffer_box_t bbox;
        ssd1322_framebuffer_draw_text(fbp, buf, 0, 32, 24, SSD1322_FONT_FREESANS, 6, &bbox);
        ssd1322_framebuffer_bitdump_nospace(fbp);
        ssd1322_framebuffer_clear(fbp);

    } while (0);
    if (fbp)
        ssd1322_framebuffer_destroy(fbp);
    if (errp)
        ssd1322_err_destroy(errp);
    return rc;
}