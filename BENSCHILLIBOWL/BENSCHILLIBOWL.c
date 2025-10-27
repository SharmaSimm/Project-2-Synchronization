#include "BENSCHILLIBOWL.h"

#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static bool IsEmpty(BENSCHILLIBOWL* bcb);
static bool IsFull(BENSCHILLIBOWL* bcb);
static void AddOrderToBack(Order **orders, Order *order);

/* ----- Menu ----- */
MenuItem BENSCHILLIBOWLMenu[] = {
    "BensChilli",
    "BensHalfSmoke",
    "BensHotDog",
    "BensChilliCheeseFries",
    "BensShake",
    "BensHotCakes",
    "BensCake",
    "BensHamburger",
    "BensVeggieBurger",
    "BensOnionRings",
};
int BENSCHILLIBOWLMenuLength = 10;

/* Select a random item from the Menu and return it */
MenuItem PickRandomMenuItem() {
    int idx = rand() % BENSCHILLIBOWLMenuLength;
    return BENSCHILLIBOWLMenu[idx];
}

/* Allocate memory for the Restaurant, then create the mutex and condition variables */
BENSCHILLIBOWL* OpenRestaurant(int max_size, int expected_num_orders) {
    BENSCHILLIBOWL *bcb = (BENSCHILLIBOWL*)calloc(1, sizeof(BENSCHILLIBOWL));
    if (!bcb) return NULL;

    bcb->orders               = NULL;     // empty queue
    bcb->current_size         = 0;
    bcb->max_size             = max_size;
    bcb->next_order_number    = 1;        // start order numbering at 1
    bcb->orders_handled       = 0;
    bcb->expected_num_orders  = expected_num_orders;

    pthread_mutex_init(&bcb->mutex, NULL);
    pthread_cond_init(&bcb->can_add_orders, NULL);
    pthread_cond_init(&bcb->can_get_orders, NULL);

    /* seed RNG once per process (good enough for this simulation) */
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

    printf("Restaurant is open!\n");
    return bcb;
}

/* check that the number of orders received is equal to the number handled; free resources */
void CloseRestaurant(BENSCHILLIBOWL* bcb) {
    /* No orders left in the system */
    pthread_mutex_lock(&bcb->mutex);
    assert(bcb->current_size == 0);
    assert(bcb->orders_handled == bcb->expected_num_orders);
    pthread_mutex_unlock(&bcb->mutex);

    pthread_mutex_destroy(&bcb->mutex);
    pthread_cond_destroy(&bcb->can_add_orders);
    pthread_cond_destroy(&bcb->can_get_orders);

    free(bcb);
    printf("Restaurant is closed!\n");
}

/* add an order to the back of queue */
int AddOrder(BENSCHILLIBOWL* bcb, Order* order) {
    pthread_mutex_lock(&bcb->mutex);

    /* wait until not full */
    while (IsFull(bcb)) {
        pthread_cond_wait(&bcb->can_add_orders, &bcb->mutex);
    }

    /* assign order number and enqueue */
    order->order_number = bcb->next_order_number++;
    order->next = NULL;
    AddOrderToBack(&bcb->orders, order);
    bcb->current_size++;

    /* wake a waiting cook */
    pthread_cond_signal(&bcb->can_get_orders);
    pthread_mutex_unlock(&bcb->mutex);

    return order->order_number;
}

/* remove an order from the queue; NULL when everything is done */
Order *GetOrder(BENSCHILLIBOWL* bcb) {
    pthread_mutex_lock(&bcb->mutex);

    /* wait for orders while there will still be more work;
       stop when all expected orders have been handled and queue is empty */
    while (IsEmpty(bcb) && bcb->orders_handled < bcb->expected_num_orders) {
        pthread_cond_wait(&bcb->can_get_orders, &bcb->mutex);
    }

    if (IsEmpty(bcb) && bcb->orders_handled >= bcb->expected_num_orders) {
        /* Tell other cooks to wake up and also exit */
        pthread_cond_broadcast(&bcb->can_get_orders);
        pthread_mutex_unlock(&bcb->mutex);
        return NULL;
    }

    /* pop from front */
    Order *front = bcb->orders;
    bcb->orders = front->next;
    bcb->current_size--;
    bcb->orders_handled++;

    /* a slot is free; wake a waiting customer */
    pthread_cond_signal(&bcb->can_add_orders);
    pthread_mutex_unlock(&bcb->mutex);

    return front;
}

/* ----- helpers ----- */
static bool IsEmpty(BENSCHILLIBOWL* bcb) {
    return (bcb->current_size == 0);
}

static bool IsFull(BENSCHILLIBOWL* bcb) {
    return (bcb->current_size >= bcb->max_size);
}

/* append to singly-linked list queue (tail insert) */
static void AddOrderToBack(Order **orders, Order *order) {
    if (*orders == NULL) {
        *orders = order;
        return;
    }
    Order *p = *orders;
    while (p->next) p = p->next;
    p->next = order;
}
