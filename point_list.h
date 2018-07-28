struct Point {
  int x;
  int y;
};

struct PointListElement {
  struct Point point;
  struct PointListElement* next;
};

typedef struct PointListElement* PointList;

void point_list_insert(struct PointListElement* el, struct PointListElement* new_el);
struct PointListElement* point_list_new_element(int x, int y);
