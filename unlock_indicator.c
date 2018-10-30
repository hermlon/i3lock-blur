/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <X11/Xlib.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>
#include <ev.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#include "blur.h"
#include "i3lock.h"
#include "unlock_indicator.h"
#include "xcb.h"
#include "randr.h"

#include "point_list.h"

#define BUTTON_RADIUS 300
#define BUTTON_SPACE (BUTTON_RADIUS + 5)
#define BUTTON_CENTER (BUTTON_RADIUS + 5)
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

extern bool debug_mode;

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
int input_position;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];
uint32_t button_diameter_physical;

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* List of pressed modifiers, or NULL if none are pressed. */
extern char *modifier_string;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;
extern bool dpms_capable;
/* Whether the image should be tiled. */
extern bool tile;
/* Whether to use fuzzy mode. */
extern bool fuzzy;
extern int blur_radius;
extern float blur_sigma;
/* to blur the screen only once */
extern bool once;
/* The background color to use (in hex). */
extern char color[7];
extern Display *display;

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;
/* Number of failed unlock attempts. */
extern int failed_attempts;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
auth_state_t auth_state;

/* A surface for the unlock indicator */
static cairo_surface_t *unlock_indicator_surface = NULL;

static PointList coords = NULL;

/*
 * Returns the scaling factor of the current screen. E.g., on a 227 DPI MacBook
 * Pro 13" Retina screen, the scaling factor is 227/96 = 2.36.
 *
 */
static double scaling_factor(void) {
    const int dpi = (double)screen->height_in_pixels * 25.4 /
                    (double)screen->height_in_millimeters;
    return (dpi / 96.0);
}

void add_fractal_iteration(struct PointListElement* element) {
  int x_start = element->point.x;
  int y_start = element->point.y;
  int x_end = element->next->point.x;
  int y_end = element->next->point.y;

  // vector from start to end
  int x_vec = x_end - x_start;
  int y_vec = y_end - y_start;

  int x_new_1 = x_vec / 3;
  int y_new_1 = y_vec / 3;

  int x_new_2 = x_vec / 3 * 2;
  int y_new_2 = y_vec / 3 * 2;

  // the third of the whole length
  int third_length = round(sqrt(pow(x_new_1, 2) + pow(y_new_1, 2)));
  // the length of the new triangle 
  int length = sqrt(pow(third_length, 2) - pow(third_length, 2) / 4);

  // outstanding triangle point
  // position vector of the middle
  int x_middle_point = x_start + x_vec / 2;
  int y_middle_point = y_start + y_vec / 2;
  // a orthogonal vector to vec
  int x_orth = y_vec;
  int y_orth = -x_vec;
  // with a specific length
  int x_new_3 = x_orth * length / round(sqrt(pow(x_orth, 2) + pow(y_orth, 2)));
  int y_new_3 = y_orth * length / round(sqrt(pow(x_orth, 2) + pow(y_orth, 2)));

  struct PointListElement* new_1 =
    point_list_new_element(x_start + x_new_1, y_start + y_new_1);
  point_list_insert(element, new_1);

  struct PointListElement* new_3 =
    point_list_new_element(x_middle_point + x_new_3, y_middle_point + y_new_3);
  point_list_insert(new_1, new_3);

  struct PointListElement* new_2 =
    point_list_new_element(x_start + x_new_2, y_start + y_new_2);
  point_list_insert(new_3, new_2);
}

struct PointListElement* get_regular_polygon(int vertices, int radius) {
  struct PointListElement* coords = NULL;
  struct PointListElement* last;
  // vertices + 1 to close the shape
  for (size_t i = 0; i < vertices + 1; i++) {
    /* add comment explaining this formular when it works.
     It does -> no need to explain it anymore */
    int x = round(radius * sin((i+1) * 2 * M_PI / vertices));
    int y = round(radius * cos((i+1) * 2 * M_PI / vertices));
    struct PointListElement* point =
      point_list_new_element(BUTTON_CENTER + x, BUTTON_CENTER - y);
    if(coords != NULL) {
      point_list_insert(last, point);
    }
    else {
      coords = point;
    }
    last = point;
  }
  return coords;
}

static void draw_point_list(cairo_t* ctx, PointList next) {
  /* draw lines between points */
  cairo_move_to(ctx, next->point.x, next->point.y);
  int i = 0;
  while(next != 0) {
    cairo_line_to(ctx, next->point.x, next->point.y);
    cairo_stroke(ctx);
    cairo_move_to(ctx, next->point.x, next->point.y);
    next = next->next;
    i ++;
  }
}

static int get_random_pos_level(struct PointListElement* coords) {
  int randpos = -1;
  do {
     int rp = rand() % point_list_size(coords);
     int level = point_list_get(coords, rp)->level;

     // chance gets lower the higher the level gets
     if(rand() % level == 0) {
       randpos = rp;
     }
  } while(randpos == -1);
  return randpos;
}

static void draw_fractal() {
  if (unlock_indicator_surface == NULL) {
      button_diameter_physical = ceil(scaling_factor() * BUTTON_DIAMETER);
      DEBUG("scaling_factor is %.f, physical diameter is %d px\n",
            scaling_factor(), button_diameter_physical);
      unlock_indicator_surface = cairo_image_surface_create(
          CAIRO_FORMAT_ARGB32, button_diameter_physical,
          button_diameter_physical);
  }

  cairo_t *ctx = cairo_create(unlock_indicator_surface);
  /* clear the surface */
  cairo_save(ctx);
  cairo_set_operator(ctx, CAIRO_OPERATOR_CLEAR);
  cairo_paint(ctx);
  cairo_restore(ctx);

  if (unlock_indicator &&
      (unlock_state >= STATE_KEY_PRESSED || auth_state > STATE_AUTH_IDLE)) {

    cairo_set_source_rgb(ctx, 255, 255, 255);
    cairo_set_line_width(ctx, 2.0);

    /////////////////////////////////////////////////////////////////////////////

    // initialize if first call
    if(coords == NULL) {
      coords = get_regular_polygon(3, 90);
    }

    /* Use the appropriate color for the different PAM states
     * (currently verifying, wrong password, or default) */
    switch (auth_state) {
        case STATE_AUTH_VERIFY:
        case STATE_AUTH_LOCK:
            cairo_set_source_rgba(ctx, 0, 114.0 / 255, 255.0 / 255, 0.75);
            break;

