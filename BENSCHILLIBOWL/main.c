#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

#include "BENSCHILLIBOWL.h"

// Tunables for testing
#define BENSCHILLIBOWL_SIZE 100
#define NUM_CUSTOMERS 90
#define NUM_COOKS 10
#define ORDERS_PER_CUSTOMER 3
#define EXPECTED_NUM_ORDERS (NUM_CUSTOMERS * ORDERS_PER_CUSTOMER)

// Global restaurant
BENSCHILLIBOWL *bcb;

/**
 * Customer thread:
 *  - allocate an Order
 *  - pick a menu item
 *  - set fields (item, customer_id)
 *  - add to restaurant
 */
void* BENSCHILLIBOWLCustomer(void* tid) {
    int customer_id = (int)(long)tid;

    for (int i = 0; i < ORDERS_PER_CUSTOMER; i++) {
        Order *ord = (Order*)malloc(sizeof(Order));
        ord->menu_item   = PickRandomMenuItem();
        ord->customer_id = customer_id;
        ord->order_number = 0;
        ord->next = NULL;

        int onum = AddOrder(bcb, ord);
        (void)onum; // number assigned; not required to print

        /* tiny think-time to increase interleaving */
        usleep(1000 * (rand() % 10));
    }
    return NULL;
}

/**
 * Cook thread:
 *  - keep getting orders until GetOrder returns NULL
 *  - "fulfill" then free the order
 */
void* BENSCHILLIBOWLCook(void* tid) {
    int cook_id = (int)(long)tid;
    int orders_fulfilled = 0;

    for (;;) {
        Order *ord = GetOrder(bcb);
        if (ord == NULL) break;               // no more work

        // Simulate cooking (optional)
        // usleep(1000 * (rand() % 20));

        free(ord);
        orders_fulfilled++;
    }

    printf("Cook #%d fulfilled %d orders\n", cook_id, orders_fulfilled);
    return NULL;
}

/**
 * Program entry:
 *  - open restaurant
 *  - start customers and cooks
 *  - join all threads
 *  - close restaurant
 */
int main() {
    bcb = OpenRestaurant(BENSCHILLIBOWL_SIZE, EXPECTED_NUM_ORDERS);

    pthread_t customers[NUM_CUSTOMERS];
    pthread_t cooks[NUM_COOKS];

    // spawn cooks first or customers first—either works
    for (int i = 0; i < NUM_COOKS; i++) {
        pthread_create(&cooks[i], NULL, BENSCHILLIBOWLCook, (void*)(long)(i+1));
    }
    for (int i = 0; i < NUM_CUSTOMERS; i++) {
        pthread_create(&customers[i], NULL, BENSCHILLIBOWLCustomer, (void*)(long)(i+1));
    }

    // wait for customers to finish adding orders
    for (int i = 0; i < NUM_CUSTOMERS; i++) {
        pthread_join(customers[i], NULL);
    }

    // wait for cooks to finish consuming all orders
    for (int i = 0; i < NUM_COOKS; i++) {
        pthread_join(cooks[i], NULL);
    }

    CloseRestaurant(bcb);
    return 0;
}
