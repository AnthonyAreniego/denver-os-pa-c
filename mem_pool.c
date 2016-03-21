/*
 * Created by Ivo Georgiev on 2/9/16.
 * Modified/Updated by Anthony Areniego on 3/20/2016
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;



/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {
    // ensure that it's called only once until mem_free
    // allocate the pool store with initial capacity
    // note: holds pointers only, other functions to allocate/deallocate

    pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;

    if(pool_store == NULL)
    {
        pool_store = (pool_mgr_pt*)calloc(pool_store_size, sizeof(pool_mgr_pt));


        if(pool_store == NULL)
        {
           printf("Allocation failed.");
           return ALLOC_CALLED_AGAIN;
        }
        else
        {
           return ALLOC_OK;
        }
    }

    return ALLOC_FAIL;
}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    // make sure all pool managers have been deallocated
    // can free the pool store array
    // update static variables

        if(pool_store != NULL)
    {
        free(pool_store);
        pool_store_capacity = 0;
        return ALLOC_OK;
    }
    else
    {
        printf("Memory already deallocated");
        return ALLOC_CALLED_AGAIN;
    }
    return ALLOC_FAIL;

    return ALLOC_FAIL;
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // make sure there the pool store is allocated
    if(pool_store == NULL)
    {
        printf("pool store not allocated");
        return NULL;
    }
    // expand the pool store, if necessary
    if(pool_store_size > pool_store_capacity)
    {
        pool_store_capacity += MEM_EXPAND_FACTOR;
    }
    // allocate a new mem pool mgr
    pool_mgr_pt mem_pool_mgr = (pool_mgr_pt*) malloc(sizeof(pool_mgr_t));
    // check success, on error return null
    if(mem_pool_mgr == NULL)
    {
        printf("allocation failure");
        return NULL;
    }
    // allocate a new memory pool
    mem_pool_mgr->pool.mem = (char*) malloc(size);

    // check success, on error deallocate mgr and return null
    if(mem_pool_mgr->pool.mem == NULL)
    {
        printf("allocation failure");
        free(mem_pool_mgr);
        return NULL;
    }

    // allocate a new node heap
    mem_pool_mgr->node_heap = (node_pt) calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));

    // check success, on error deallocate mgr/pool and return null
    if(mem_pool_mgr->node_heap == NULL)
    {
        printf("allocation failure");
        free(mem_pool_mgr->pool.mem);
        free(mem_pool_mgr);
        return NULL;
    }
    // allocate a new gap index
    mem_pool_mgr->gap_ix = (gap_pt) calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    // check success, on error deallocate mgr/pool/heap and return null
    if(mem_pool_mgr->node_heap == NULL)
    {
        printf("allocation failure");
        free(mem_pool_mgr->node_heap);
        free(mem_pool_mgr->pool.mem);
        free(mem_pool_mgr);
        return NULL;
    }
    // assign all the pointers and update meta data:
    //   initialize top node of node heap
    mem_pool_mgr->node_heap[0].alloc_record.size = size;
    mem_pool_mgr->node_heap[0].alloc_record.mem = mem_pool_mgr->pool.mem;
    mem_pool_mgr->node_heap[0].used = 0;
    mem_pool_mgr->node_heap[0].allocated = 0;
    mem_pool_mgr->node_heap[0].next = NULL;
    mem_pool_mgr->node_heap[0].prev = NULL;

    //   initialize top node of gap index
    mem_pool_mgr->gap_ix[0].size = size;
    mem_pool_mgr->gap_ix[0].node = mem_pool_mgr->node_heap;

    //   initialize pool mgr
    mem_pool_mgr->pool.policy = policy;
    mem_pool_mgr->pool.num_gaps = 1;
    mem_pool_mgr->pool.alloc_size = 0;
    mem_pool_mgr->pool.num_allocs = 0;
    mem_pool_mgr->pool.total_size = size;
    mem_pool_mgr->used_nodes = 1;
    //   link pool mgr to pool store

    int i = 0;
    while(pool_store[i] != NULL)
    {
        pool_store[i] = mem_pool_mgr;
        pool_store_size = 1;
        ++i;
    }



    // return the address of the mgr, cast to (pool_pt)
    return (pool_pt) mem_pool_mgr;
}

alloc_status mem_pool_close(pool_pt pool) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mem_pool_mgr = (pool_mgr_pt)pool;
    // check if this pool is allocated
    if(mem_pool_mgr == NULL)
    {
        return ALLOC_NOT_FREED;
    }
    // check if pool has only one gap
    if(pool->num_gaps > 1)
    {
        return ALLOC_NOT_FREED;
    }
    // check if it has zero allocations
    if(pool->num_gaps > 0);
    {
        return ALLOC_NOT_FREED;
    }
    // free memory pool
    free(pool);
    // free node heap
    free(mem_pool_mgr->node_heap);
    // free gap index
    free(mem_pool_mgr->gap_ix);
    // find mgr in pool store and set to null
    int i = 0;
    while(pool_store[i] != mem_pool_mgr)
    {
        pool_store[i] = NULL;
        ++i;
    }
    // note: don't decrement pool_store_size, because it only grows
    // free mgr
    free(mem_pool_mgr);

    return ALLOC_OK;
}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mem_pool_mgr = (pool_mgr_pt) pool;
    // check if any gaps, return null if none
    if(pool->num_gaps == 0)
    {
        return NULL;
    }
    // expand heap node, if necessary, quit on error
    if(mem_pool_mgr->total_nodes < mem_pool_mgr->used_nodes)
    {
        mem_pool_mgr->total_nodes += MEM_NODE_HEAP_EXPAND_FACTOR;
    }
    // check used nodes fewer than total nodes, quit on error
    if(mem_pool_mgr->total_nodes < mem_pool_mgr->used_nodes)
    {
        return NULL;
    }
    // get a node for allocation:
    node_pt nodeA = NULL;
    node_pt gapA = NULL;
    int i = 0;
    // if FIRST_FIT, then find the first sufficient node in the node heap
    if(mem_pool_mgr->pool.policy == FIRST_FIT)
    {
    while(i < mem_pool_mgr->total_nodes)
            if((mem_pool_mgr->node_heap[i].allocated != 0 || mem_pool_mgr->node_heap[i].alloc_record.size < size))
            ++i;
            nodeA = &mem_pool_mgr->node_heap[i];
    }
    // if BEST_FIT, then find the first sufficient node in the gap index
    if(mem_pool_mgr->pool.policy == BEST_FIT)
    {
    while(i < mem_pool_mgr->gap_ix->size)
            if(i < mem_pool_mgr->pool.num_gaps && mem_pool_mgr->gap_ix[i + 1].size >= size)
            ++i;
            nodeA = mem_pool_mgr->gap_ix[i].node;
    }
    // check if node found
    if (nodeA = NULL)
    {
        return NULL;
    }
    // update metadata (num_allocs, alloc_size)
    (mem_pool_mgr->pool.num_allocs)++;
    mem_pool_mgr->pool.alloc_size += size;
    // calculate the size of the remaining gap, if any
    int remainGap = 0;
    if(nodeA->alloc_record.size - size > 0)
    {
        remainGap = nodeA->alloc_record.size - size;
    }
    // remove node from gap index
    _mem_remove_from_gap_ix(mem_pool_mgr, size, nodeA);
    // convert gap_node to an allocation node of given size
    nodeA->alloc_record.size = size;
    nodeA->allocated = 1;
    nodeA->used = 1;
    // adjust node heap:
    //   if remaining gap, need a new node
    //   find an unused one in the node heap
    //   make sure one was found
    //   initialize it to a gap node
    int j = 0;
    if(remainGap > 0)
    {
        while(mem_pool_mgr->node_heap[j].used > 0)
        {
            gapA = &mem_pool_mgr->node_heap[j];
            ++j;
        }
    }

    //   update metadata (used_nodes)
    (mem_pool_mgr->used_nodes)++;
    //   update linked list (new node right after the node for allocation)
    if(nodeA->next != NULL)
    {
        nodeA->next->prev = gapA;
        gapA->prev = nodeA;
    }

    //   add to gap index
    _mem_add_to_gap_ix(mem_pool_mgr, remainGap, gapA);
    //   check if successful
    // return allocation record by casting the node to (alloc_pt)
    return (alloc_pt) nodeA;

}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mem_pool_mgr = (pool_mgr_pt) pool;
    // get node from alloc by casting the pointer to (node_pt)
    node_pt node = (node_pt) alloc;
    // find the node in the node heap
    for(int i = 0; i< mem_pool_mgr->total_nodes; ++i)
    {
        if(node == &mem_pool_mgr->node_heap[i])
        {
            node = &mem_pool_mgr->node_heap[i];
        }
    }
    // this is node-to-delete
    // make sure it's found
    if(node == NULL)
    {
        return ALLOC_FAIL;
    }
    // convert to gap node
    // update metadata (num_allocs, alloc_size)
    mem_pool_mgr->pool.num_allocs--;
    mem_pool_mgr->pool.alloc_size -= node->alloc_record.size;
    // if the next node in the list is also a gap, merge into node-to-delete
    //   remove the next node from gap index
    //   check success
    //   add the size to the node-to-delete
    //   update node as unused
    //   update metadata (used nodes)
    //   update linked list:
    /*
                    if (next->next) {
                        next->next->prev = node_to_del;
                        node_to_del->next = next->next;
                    } else {
                        node_to_del->next = NULL;
                    }
                    next->next = NULL;
                    next->prev = NULL;
     */

    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    //   remove the previous node from gap index
    //   check success
    //   add the size of node-to-delete to the previous
    //   update node-to-delete as unused
    //   update metadata (used_nodes)
    //   update linked list
    /*
                    if (node_to_del->next) {
                        prev->next = node_to_del->next;
                        node_to_del->next->prev = prev;
                    } else {
                        prev->next = NULL;
                    }
                    node_to_del->next = NULL;
                    node_to_del->prev = NULL;
     */
    //   change the node to add to the previous node!
    // add the resulting node to the gap index
    // check success

    return ALLOC_FAIL;
}

void mem_inspect_pool(pool_pt pool,
                      pool_segment_pt *segments,
                      unsigned *num_segments) {
    // get the mgr from the pool
    pool_mgr_pt mem_pool_mgr = (pool_mgr_pt) pool;
    node_pt currentNode;
    // allocate the segments array with size == used_nodes
    pool_segment_pt segArr = (pool_segment_pt) calloc(mem_pool_mgr->used_nodes, sizeof(pool_segment_t));
    currentNode = mem_pool_mgr->node_heap;

    // loop through the node heap and the segments array
    for(int i=0; i < mem_pool_mgr->used_nodes; ++i)
    {
        segArr[i].size = currentNode->alloc_record.size;
        segArr[i].allocated = currentNode->allocated;
        if(currentNode->next != NULL)
        {
            currentNode = currentNode->next;
        }
    }
    //    for each node, write the size and allocated in the segment
    // "return" the values:
    *segments = segArr;
    *num_segments = mem_pool_mgr->used_nodes;

}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
static alloc_status _mem_resize_pool_store() {
    // check if necessary
    if (((float) pool_store_size / pool_store_capacity)> MEM_POOL_STORE_FILL_FACTOR)
        {
            pool_store = realloc(pool_store, (sizeof(pool_store)* MEM_POOL_STORE_EXPAND_FACTOR));
            pool_store = pool_store_capacity * MEM_POOL_STORE_EXPAND_FACTOR;
            return ALLOC_OK;
        }

    // don't forget to update capacity variables


    return ALLOC_FAIL;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    // see above
    if (((float) pool_mgr->used_nodes / pool_mgr->total_nodes)> MEM_NODE_HEAP_FILL_FACTOR)
    {
        pool_mgr->node_heap = realloc(pool_mgr->node_heap, sizeof(pool_mgr->node_heap) * MEM_NODE_HEAP_EXPAND_FACTOR);
        pool_mgr->total_nodes = pool_mgr->total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR;

        return ALLOC_OK;
    }
    return ALLOC_FAIL;
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    // see above
if (((float) pool_mgr->pool.num_gaps / pool_mgr->gap_ix_capacity)> MEM_GAP_IX_FILL_FACTOR)
    {
        pool_mgr->gap_ix = realloc(pool_mgr->gap_ix, sizeof(pool_mgr->gap_ix) * MEM_GAP_IX_EXPAND_FACTOR);
        pool_mgr->gap_ix_capacity = pool_mgr->gap_ix_capacity * MEM_GAP_IX_EXPAND_FACTOR;
        return ALLOC_OK;
    }
    return ALLOC_FAIL;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {
    int i = 0;
    // expand the gap index, if necessary (call the function)
    _mem_resize_gap_ix(pool_mgr);
    // add the entry at the end
    pool_mgr->gap_ix[i].size = size;
    pool_mgr->gap_ix[i].node = node;
    // update metadata (num_gaps)
    (pool_mgr->pool.num_gaps)++;
    // sort the gap index (call the function)
    // check success

    return ALLOC_FAIL;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    // find the position of the node in the gap index
    // loop from there to the end of the array:
    //    pull the entries (i.e. copy over) one position up
    //    this effectively deletes the chosen node
    // update metadata (num_gaps)
    // zero out the element at position num_gaps!

        int i = 0;
    while (pool_mgr->gap_ix[i].node != node) {
        ++i;
    }

    // update metadata (num_gaps)
    pool_mgr->pool.num_gaps--;
    // zero out the element at position num_gaps!
    pool_mgr->gap_ix[i].size = 0;
    pool_mgr->gap_ix[i].node = NULL;

    return ALLOC_FAIL;
}

// note: only called by _mem_add_to_gap_ix, which appends a single entry
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:
    //    if the size of the current entry is less than the previous (u - 1)
    //    or if the sizes are the same but the current entry points to a
    //    node with a lower address of pool allocation address (mem)
    //       swap them (by copying) (remember to use a temporary variable)

   gap_t tempNode;
    int i = pool_mgr->pool.num_gaps - 1;
    while (pool_mgr->gap_ix[i].size < pool_mgr->gap_ix[i+1].size && i > 0){
        tempNode = pool_mgr->gap_ix[i];
        pool_mgr->gap_ix[i] = pool_mgr->gap_ix[i+1];
        pool_mgr->gap_ix[i+1] = tempNode;
        --i;
    }
    return ALLOC_OK;
    return ALLOC_FAIL;
}


