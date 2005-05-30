#ifndef HSCORE_ITEMS_H
#define HSCORE_ITEMS_H

#define I_HSCORE_ITEMS "hscore_items-3"

//callback
#define CB_EVENT_ACTION "eventaction"

//callback function prototype
typedef void (*eventActionFunction)(Player *p, int eventID);

typedef struct Ihscoreitems
{
	INTERFACE_HEAD_DECL

	int (*getItemCount)(Player *p, Item *item, int ship);
	void (*addItem)(Player *p, Item *item, int ship, int amount);

	Item * (*getItemByName)(const char *name, Arena *arena);

	int (*getPropertySum)(Player *p, int ship, const char *prop); //properties ARE case sensitive

	void (*triggerEvent)(Player *p, int ship, const char *event);
	void (*triggerEventOnItem)(Player *p, Item *item, int ship, const char *event);

	int (*getFreeItemTypeSpots)(Player *p, ItemType *type, int ship);

	//more required, i'm sure
} Ihscoreitems;

#endif //HSCORE_ITEMS_H
