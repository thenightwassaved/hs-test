#ifndef HSCORE_TYPES_H
#define HSCORE_TYPES_H

/*

typedef struct Info
{
    char name[32];
    char description[256];
} Info;

typedef struct Property
{
    Info info;

    int value;
} Property;

typedef struct Item
{
    Info info;

    int price;
    int sellPrice;

    LinkedList propertyList;
} Item;

*/

typedef enum EventAction
{
	//removes event->data amount of the items from the ship's inventory
	REMOVE_ITEM,

	//removes event->data amount of the item's ammo type from inventory
	REMOVE_ITEM_AMMO,

	//sends prize #event->data to the player
	PRIZE,

	//sets the item's inventory data to event->data. This is useful with
	//the "purchace" event.
	SET_INVENTORY_DATA,

	//does a ++ on inventory data.
	DECREMENT_INVENTORY_DATA,

	//does a -- on inventory data. A "datazero" event may be generated as a result.
	DECREMENT_INVENTORY_DATA,

	//Specs the player.
	SPEC,

	//we need a lot more
} EventAction

typedef struct Event
{
	char event[16]; //something like "death" or "datazero"
	EventAction action;

	int data; //action dependent

	char message[200]; //if == to "" then nothing will be sent.
} Event;

typedef struct Property
{
	char name[32];
	int value;
} Property;

typedef struct ItemType
{
	char name[32];
	int max; //maximum total of this item type on a ship before ?buy denies purchace

	int id; //MySQL use only
} ItemType;

typedef struct Item
{
	char name[16];
	char shortDesc[32]; //displayed inline in the ?buy menu
	char longDesc[200]; //displayed as part of ?iteminfo
	int buyPrice;
	int sellPrice;

	int expRequired; //requirement to own

	int shipsAllowed; //bit positions represent each ship. bit 0 = warbird.

	LinkedList *propertyList;

	LinkedList *eventList;

	ItemType *type1, type2;
	int typeDelta1, typeDelta2;

	//if changes to this item should be delayed until a complete save (like on exit).
	//This is a necessity when dealing with ammo. We don't want to update MySQL every
	//time a gun is fired.
	int delayStatusWrite;

	Item *ammo; //can be NULL, only for use by events.

	int id; //MySQL use only
} Item;

typedef struct InventoryEntry
{
	Item *item;
	int count;

	int data; //persistent int for use by the event system.
} InventoryEntry;

typedef struct ShipHull
{
	LinkedList *inventoryEntryList

	//NOTE: no need for ship #, as it's defined by the array index (when loaded by hscore_database)

	//if we compile a hashmap of properties, it can go in here.

	int id; //MySQL use only
} ShipHull;

typedef struct Category
{
	char name[32]; //displayed on ?buy
	char description[64]; //displayed inline on the ?buy menu

	LinkedList *itemList; //a list of member items that are displayed
} Category;

typedef struct Store
{
	char name[32]; //displayed in ?buysell location errors
	char description[200]; //displayed in ?storeinfo
	char region[16]; //region that defines the store

	LinkedList *itemList; //a list of items that can be purchaced here
} Store;

#endif //HSCORE_TYPES_H