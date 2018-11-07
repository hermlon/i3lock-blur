struct Point {
  int x;
  int y;
};

struct PointListElement {
  struct Point point;
  struct PointListElement* next;
  int level;
};

typedef struct PointListElement* PointList;

void point_list_insert(struct PointListElement* el, struct PointListElement* new_el);
void point_list_remove(struct PointListElement* remove_el);
struct PointListElement* point_list_new_element(int x, int y);
struct PointListElement* point_list_get(struct PointListElement* pointlist, int index);
int point_list_size(struct PointListElement* pointlist);
int point_list_get_new_level(struct PointListElement* el);
