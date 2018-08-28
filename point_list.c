#include "point_list.h"
#include <stdlib.h>

void point_list_insert(struct PointListElement* el, struct PointListElement* new_el) {
  new_el->next = el->next;
  el->next = new_el;
}

struct PointListElement* point_list_get(struct PointListElement* pointlist, int index) {
  struct PointListElement* last = pointlist;
  for(int i = 0; i < index; i ++) {
      last = last->next;
  }
  return last;
}

struct PointListElement* point_list_new_element(int x, int y) {
  struct PointListElement* ple = malloc(sizeof(struct PointListElement));
  ple->point.x = x;
  ple->point.y = y;
  ple->next = 0;
  return ple;
}
