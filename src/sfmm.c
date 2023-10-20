/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

static int first_call = 0;
static int added_page=0;

// function to check nth bit
// int isNthBitSet(unsigned int data, unsigned int pos) { return ((data & (1 << pos)) ? 1 : 0); }

// function to calculate free list index for a block with SIZE
int get_free_list_index(int size)
{
    int upper_limit = 0;
    for (int i = 0; i < 10; i++)
    {
        upper_limit = (1 << i) * 32;
        if (size <= upper_limit)
        {
            return i;
        }
        else if (size > (256 * 32))
        {
            return 9;
        }
    }
    return -1;
}

// helper function: insert a block into the free list
void free_list_insert(int index, sf_block *block)        ///*********
{
    // manipulate pre/next settings
    sf_block *old_first = sf_free_list_heads[index].body.links.next;
    old_first->body.links.prev = block;
    block->body.links.next = old_first;
    block->body.links.prev = &sf_free_list_heads[index];
    sf_free_list_heads[index].body.links.next = block;

    (block->header) &= ~(1 << 0); // clear block alloc bit (=0)
    (block->header) &= ~(1 << 2); // clear in quick list bit (=0)

    // add footer for block
    int realSize = (block->header) & ~(0x7); // *************calculate real block size !!!**************
    sf_footer *blockFooter = (sf_footer *)((void *)block + realSize - 8);
    *blockFooter = block->header;

    // calculate next block position, set next block prev alloc bit =0
    sf_block *nextBlock = (sf_block *)((void *)block + (int)realSize);
    (nextBlock->header) &= ~(1 << 1); // clear next block's prev alloc bit (=0)

    int nextRealSize = nextBlock->header & ~(0x7); // calculate real block size !!!

    int allocBit = (nextBlock->header) & (1 << 0); // calculate next block alloc bit
    if (allocBit == 0)
    { // if alloc bit =0, free block, has footer, update footer bit as well
        sf_footer *nextFooter = (sf_footer *)((void *)nextBlock + nextRealSize - 8);
        *nextFooter = nextBlock->header; // next block footer content = next header
    }
}

// helper function: help malloc to initialize prologue and epilogue when first call
void sf_initialize()
{
    void *mem_start = sf_mem_start();
    void *mem_end = sf_mem_end();
    // debug("mem_start address: %p\n", mem_start);
    // debug("mem_end address: %p\n", mem_end);

    // setting up prologue
    sf_block *prolog = (sf_block *)mem_start;
    prolog->header = 32;
    prolog->header ^= 1 << 0; // set the last bit 1 as allocated block

    // int isBitSet  = isNthBitSet(prolog->header,1);         // just to check if a bit is set
    // if(isBitSet) debug("\nBit is One\n");
    // else debug("\nBit is zero\n");

    // setting up epilogue
    sf_block *epilog = (sf_block *)(mem_end - 8);
    epilog->header = 0;
    (epilog->header) ^= 1 << 0;

    // initialize free list
    for (int i = 0; i < NUM_FREE_LISTS; i++)
    {
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }

    // calculate free block
    sf_block *availableBlock = (sf_block *)(mem_start + 32);
    availableBlock->header = 4096 - 32 - 8;
    availableBlock->header ^= 1 << 1; // prev alloc of initial block =1 since prologue cannot be used

    // debug("footer & header content: %d\n", (int)availableBlock->header); // 4058

    // calculate index of the free block in free list                index =7
    int index = get_free_list_index(4056);
    // debug("index: %d\n", index);

    // put available block into free list
    free_list_insert(index, availableBlock);
}

// helper function: search the quick list and take out a block of size SIZE
sf_block *quick_list_takeout(size_t size)
{
    int index = (size - 32) / 8; // calculate quick list index

    debug("quick list index: %d\n", index);


    if(index > 19 || index < 0){              // terminate when index is not 0-19 for quick lists
        return NULL;
    }

    debug("%d\n",sf_quick_lists[index].length);

    if (sf_quick_lists[index].length != 0)
    {
        sf_quick_lists[index].length--;
        sf_block *takeout = sf_quick_lists[index].first;
        (takeout->header) &= ~(1 << 2); // in quick list bit=0

        sf_quick_lists[index].first = takeout->body.links.next; // move first to next block

        // look for next block in the HEAP
        sf_block *nextBlock = (sf_block *)(((void *)takeout) + (int)size);
        (nextBlock->header) |= 1 << 1; // prev alloc bit = 1

        int nextRealSize = nextBlock->header & ~(0x7); // ******

        // if next block is free, update footer bit as well
        int allocBit = (nextBlock->header) & (1 << 0); // calculate next block alloc bit
        if (allocBit == 0)
        { // if alloc bit =0, free block, has footer, update footer bit as well
            sf_footer *nextFooter = (sf_footer *)((void *)nextBlock + nextRealSize - 8);
            (*nextFooter) |= 1 << 1; // next block footer prev alloc=1
        }

        return takeout;
    }
    else
        return NULL;
}

// helper function: search the free list and take out a block of at least SIZE size
sf_block *free_list_takeout(size_t size, sf_block *block)
{
    int index = get_free_list_index((int)size);
    // debug("free list index for malloc: %d\n", index);
    // debug("block %s= NULL\n", block==NULL? "":"!")  ;
    // debug("block address: %p", block);

    for (int i = index; i < NUM_FREE_LISTS; i++)
    {
        debug("searched free list index: %d\n", i); // search 7

        sf_block *temp = sf_free_list_heads[i].body.links.next;

        // debug("%d\n",(int)temp->header & ~(0x7));
        // debug("%d\n",(int)size);
        // debug("%p\n", block ==NULL? 0:block);
        // debug("%s\n", block==temp? "block==temp": "block !=temp");

        if (((temp->header & ~(0x7)) >= size && block == NULL) || (block != NULL && temp == block && (temp->header & ~(0x7)) == size))
        {
            debug("temp header: %d, size: %d", (int)temp->header, (int)size);

            // change the prev/next pointers
            sf_block *PrevBlock = temp->body.links.prev;
            sf_block *NextBlock = temp->body.links.next; // *** big N in list ***
            PrevBlock->body.links.next = NextBlock;
            NextBlock->body.links.prev = PrevBlock;

            (temp->header) |= 1 << 0;             // set alloc bit = 1
            int realSize = temp->header & ~(0x7); // calculate real block size!!!

            sf_footer *tempFooter = (sf_footer *)((void *)temp + realSize - 8);

            // debug("size of footer: %d\n", (int)sizeof(sf_footer));
            *tempFooter = 0; // footer needs to be deleted while taking out from free list

            // manipulate next block bits
            sf_block *nextBlock = (sf_block *)((void *)temp + realSize); // *** small n in heap ***
            (nextBlock->header) |= 1 << 1;                               // next block header prev alloc=1

            realSize = nextBlock->header & ~(0x7); // calculate next block real size !!!

            int allocBit = (nextBlock->header) & (1 << 0);
            debug("alloc bit of next block: %d\n", allocBit);
            if (allocBit == 0)
            {
                sf_footer *nextFooter = (sf_footer *)((void *)nextBlock + realSize - 8);
                (*nextFooter) |= 1 << 1; // next block footer prev alloc=1
            }

            return temp;
        }
        while (temp->body.links.next != &sf_free_list_heads[i])
        {
            temp = temp->body.links.next; // move on to next block
            if (((temp->header & ~(0x7)) >= size && block == NULL) || (block != NULL && temp == block && (temp->header & ~(0x7)) == size))
            {
                // change the prev/next pointers
                sf_block *PrevBlock = temp->body.links.prev;
                sf_block *NextBlock = temp->body.links.next; // *** big N in list ***
                PrevBlock->body.links.next = NextBlock;
                NextBlock->body.links.prev = PrevBlock;

                (temp->header) |= 1 << 0;             // set alloc bit = 1
                int realSize = temp->header & ~(0x7); // calculate real block size!!!

                sf_footer *tempFooter = (sf_footer *)((void *)temp + realSize - 8);

                *tempFooter = 0; // footer needs to be deleted while taking out from free list

                // manipulate next block bits
                sf_block *nextBlock = (sf_block *)((void *)temp + realSize); // *** small n in heap ***
                (nextBlock->header) |= 1 << 1;

                realSize = nextBlock->header & ~(0x7); // calculate real block size!!!

                int allocBit = (nextBlock->header) & (1 << 0);
                debug("alloc bit of next block: %d\n", allocBit);
                if (allocBit == 0)
                {
                    sf_footer *nextFooter = (sf_footer *)((void *)nextBlock + realSize - 8);
                    (*nextFooter) |= 1 << 1; // next block footer prev alloc=1
                }

                return temp;
            }
        }
    }
    // sf_show_heap();
    return NULL;
}

// helper function: coalesce a current block with the previous block, then insert the new block back to free list
sf_block *coalesce_prev(sf_block *blk)
{

    debug("***start coalesce with prev block***\n");

    // take out prev_alloc bit to see if coalesce is available
    int prev_alloc = (int)(blk->header & (1 << 1));

    if (prev_alloc !=0)
    { // if previous block is allocated, no coalescing

        if ((int)(blk->header & (1 << 0)) == 1)
        { // if blk is allocated, put into free list
    
            int index = get_free_list_index(blk->header & ~(0x7));
debug("here\n");
            free_list_insert(index, blk);
        }
debug("262 cannot coalesce\n");
        return blk; // return original block
    }

    sf_footer *prevFooter = (sf_footer *)((void *)blk - 8); // else if previou block is not allocated, look for prev footer
    int footerValue = (*prevFooter) & ~(0x7);               // calculate the actual size of prev block
    debug("footer value = %d\n", footerValue);

    sf_block *PrevBlock = (sf_block *)((void *)blk - footerValue); // then look for previous block
    int prevBit = (PrevBlock->header) & (1 << 0);                  // look for alloc bit

    if (PrevBlock->header != *prevFooter || prevBit == 1)
    { // return original block if header value does not match footer value or prev block is allocated

        if ((int)(blk->header & (1 << 0)) == 1)
        { // if blk is allocated, put into free list
            int index = get_free_list_index(blk->header & ~(0x7));
            free_list_insert(index, blk);     
        }
debug("280 cannot coalesce\n");
        return blk;
    }

    // ----------------------now begin coalescing--------------------------------

    sf_block *temp = free_list_takeout((size_t)(PrevBlock->header & ~(0x7)), PrevBlock); // try taking prev block out from free list

    if (temp == NULL)
    { // if take out unsuccessful, return original block

        if ((int)(blk->header & (1 << 0)) == 1)
        { // if current blk is allocated, put into free list
            int index = get_free_list_index(blk->header & ~(0x7));
            free_list_insert(index, blk);
        }

debug("297 cannot coalesce\n");
        return blk;
    }

    int alloc = (int)(blk->header & (1 << 0)); // calculate input blk's alloc bit
    if (alloc == 0)
    { // if alloc bit = 0, meaning in free list, need to take out first
        temp = free_list_takeout((size_t)(blk->header & ~(0x7)), blk);

        if (temp == NULL)
        { // if take out unsuccessful, return original block

            if ((int)(blk->header & (1 << 0)) == 1)
            { // if current blk is allocated, put into free list
                int index = get_free_list_index(blk->header & ~(0x7));
                free_list_insert(index, blk);
            }
debug("315 cannot coalesce\n");
            return blk;
        }
    }

    PrevBlock->header = (PrevBlock->header) + (blk->header & ~(0x7)); // calculate the new block size and insert into prev block, keeping prev alloc bit
   
    int index = get_free_list_index((int)(PrevBlock->header & ~(0x7))); // use new header size to calculate index in free list
    free_list_insert(index, PrevBlock);                                 // insert the new block back into the free list
//  sf_show_heap();
    return PrevBlock;
}

// helper function : split a block into two, return the first part of the block as allocated, insert the second part into the free list
sf_block *split(size_t size, sf_block *block)
{
    if((int)size<32)                             // if block size needed is smaller than 32, allocate a block of 32
        size=32;
    else if((int)size % 8 != 0){                 // else if block is not a multiple of 8, change to accurate size
        size=(size/8+1)*8;
    }

debug("%d\n", (int)size);

    // calculate the difference between request size and block size
    int nextBlockSize = (int)(block->header & ~(0x7)) - (int)size;

debug("next block size: %d\n", nextBlockSize);

    // if difference < 32, cannot split, return the block
    if (nextBlockSize < 32)
    {
        debug("splinter! size %d, do not split!\n", nextBlockSize);
        return block;
    }

    // else if difference >= 32, split block
    block->header = (block->header)-nextBlockSize; // set old block size = request size

    sf_block *nextBlock = (sf_block *)((void *)block + (int)size); // find the address of splitted block
    nextBlock->header = nextBlockSize;                             // set splitted block size = nextBlockSize
    (nextBlock->header) |= 1 << 1;                                 // set prev alloc bit = 1

debug("%p\n", nextBlock);

    int nextIndex = get_free_list_index(nextBlockSize); // calculate the index in free list for splitted block
    free_list_insert(nextIndex, nextBlock);             // insert splitted block into the free list

    debug("split successed!\n");
    return block;
}

void *sf_malloc(size_t size)
{
    void *heap_head;
    // if first call, call mem_grow, call helper function to setup prologue and epilogue
    if (first_call == 0)
    {
        first_call = 1;
        heap_head = sf_mem_grow();
        if (heap_head == NULL)
        {
            return NULL;
        }
        sf_initialize(); // call helper function to initialize heap
    }

    // calculate the actual size needed for allocation
    // debug("request size: %d\n", (int)size);
    if (size == 0)
        return NULL;
    size = size + 8;
    if (size % 8 != 0){
        size = size / 8 + 1;
        size=size*8;
    }
    if (size < 32)
        size = 32;
    debug("final request size: %d\n", (int)size);

    // search quick list, if has right sized block, manipulate, return
    sf_block *quick_block = quick_list_takeout(size);
    if (quick_block != NULL)
    {
        debug("find a block in quick list\n ");
        return quick_block->body.payload;
    }

    debug("\n-----------here passed quick list check, did not find a quick block---------------\n");

    // search free list, while free list does not contain a large enough block
    sf_block *free_block = free_list_takeout(size, NULL);


    debug("\n-----------DONE searching for free block, start 1st round-----------\n");

    while (free_block == NULL)
    {
        added_page++;               // if newly allocated page is over 20, means mem_grow would return NULL
        if(added_page>20){          // end function
            sf_errno = ENOMEM;
            return NULL;
        }

        void *new_mem_start = sf_mem_grow(); // call sf_mem_grow to get new heap page
                                             // if(sf_mem_grow returns NULL) -->return NULL;
                                             //                              -->sf_errno=ENOMEM

        // else if(sf_mem_grow != NULL)
        //      if(exist prev free block) --> remove free block from free list
        //                                --> coalesce with this block
        //                                --> set up header. footer, epilogue
        //                                --> put the new free block into corresponding list
        new_mem_start -= 8; // move to the position of last epilogue

        // setting up the new block
        sf_block *new_mem_block = (sf_block *)new_mem_start;

        new_mem_block->header = 4096;
        new_mem_block->header |= 1 << 0;                             // set the alloc bit=1 to prevent being taken out from free list

        // create a new epilogue
        sf_block *epilog = (sf_block *)(sf_mem_end() - 8);
        epilog->header = 0;
        (epilog->header) ^= 1 << 0;

        // call coalese function
        // sf_show_heap();

        debug("\n--------------------------call coalesce function----------------------\n");
        free_block = coalesce_prev(new_mem_block);

debug("size: %d",(int)size);

        free_block = free_list_takeout(size, NULL);

        debug("\n-------------------------next round----------------------------\n");
    }

debug("----------------now show heap after getting free block------------------");
// sf_show_heap();

    debug("%p\n", free_block);

    // call split function

debug("split size : %d\n", (int)size);

    free_block = split(size, free_block);

    if (free_block == NULL)
    { // check whether split returns NULL ??? neccessary???
        debug("split block failed\n");
        sf_errno = ENOMEM;
        return NULL;
    }
    // sf_show_heap();// --------------------------------------------show heap--------------------------------
    return free_block->body.payload; // return block pointer payload;
}

//----------------------------------------------- free ---------------------------------------------------------

// helper function: insert a block into the quick list
void quick_list_insert(int index, sf_block *blk)
{
    int block_size = blk->header & ~(0x7);            // calculate input block size
    int free_index = get_free_list_index(block_size); // calculate index in free list if flush is needed

    sf_block *old_first = sf_quick_lists[index].first;
    int old_length = sf_quick_lists[index].length;

    if (old_length == 5)
    {
        for (int i = 0; i < 5; i++)
        {
            sf_block *temp = quick_list_takeout(block_size);
            free_list_takeout(free_index, temp);
        }
    }

    // when quick list length < 5, no matter flushed or not, begin insert
    sf_quick_lists[index].length++;    // length +1
    sf_quick_lists[index].first = blk; // new block take the first position
    blk->body.links.next = old_first;  // old first block take the 2nd position

    blk->header |= 1 << 2; // set in quick list and alloc = 1
    blk->header |= 1 << 0;

    sf_block *nextBlock = (sf_block *)((void *)blk + block_size);
    nextBlock->header |= 1 << 1; // set next block's prev alloc bit = 1

    int next_blk_size = nextBlock->header & ~(0x7);
    if ((nextBlock->header & (1 << 0)) == 0)
    {
        sf_footer *nextFooter = (sf_footer *)((void *)nextBlock + next_blk_size - 8); // if next block is free, find its footer
        *nextFooter = nextBlock->header;                                              // set the footer content same as header content
    }
}

// helper function: coalesce a current block with the block immediately after it
sf_block *coalesce_next(sf_block *blk)
{ // return new coalesced block that is in free list

    int blk_size = blk->header & ~(0x7); // get current block size

    sf_block *nextBlock = (sf_block *)((void *)blk + blk_size); // get next block

    int next_alloc = (int)(nextBlock->header & (1 << 0)); // calculate next alloc bit

    if (next_alloc == 1)
    { // if next block is allocated, no coalescing

        if ((int)(blk->header & (1 << 0)) == 1)
        { // if current blk is allocated, put into free list
            int index = get_free_list_index(blk->header & ~(0x7));
            free_list_insert(index, blk);
        }

        return blk;
    }

    // else if next block is not allocated, means it is in the free list, take our from free list
    sf_block *temp = free_list_takeout((size_t)(nextBlock->header & ~(0x7)), nextBlock);

    if (temp != nextBlock)
    { // if the block been taken out from free list is not next block, don't coalesce

        if ((int)(blk->header & (1 << 0)) == 1)
        { // if current blk is allocated, put into free list
            int index = get_free_list_index(blk->header & ~(0x7));
            free_list_insert(index, blk);
        }

        return blk;
    }

    // ----------------now begin coalescing--------------------------------

    int curr_alloc = (int)(blk->header & (1 << 0)); // find the alloc bit of current input blk

    if (curr_alloc == 0)
    {                                                                  // if current alloc bit = 0, meaning blk is free, in free list
        temp = free_list_takeout((size_t)(blk->header & ~(0x7)), blk); // take current blk out of the free list

        if (temp == NULL)
        { // if current blk not being taken our from free list, return the current block

            if ((int)(blk->header & (1 << 0)) == 1)
            { // if current blk is allocated, put into free list
                int index = get_free_list_index(blk->header & ~(0x7));
                free_list_insert(index, blk);
            }

            return blk;
        }
    }

    int next_blk_size = nextBlock->header & ~(0x7); // calculate next block size and add to input blk
    blk->header = blk->header + next_blk_size;

    int index = get_free_list_index((int)(blk->header & ~(0x7))); // calculate the index of new block in free list
    free_list_insert(index, blk);

    return blk;
}

void sf_free(void *pp)
{
    sf_block *ppBlock = (sf_block *)(pp - 8); // find the actual block position
    void *block_start = sf_mem_start() + 32;  // find block start/end address
    void *block_end = sf_mem_end() - 8;
    void *ppEnd = (void *)ppBlock + (int)(ppBlock->header & ~(0x7)); // find end address of give block
    sf_footer *prevFooter = (sf_footer *)(pp - 16);                  // get prev block footer position

    // check abort circumstances
    if (pp == NULL) // pointer cannot be NULL
        abort();
    else if (((uintptr_t)pp & 0x7) != 0) // payload 8 bytes aligned means header 8 bytes aligned
        abort();
    else if ((ppBlock->header & ~(0x7)) < 32) // block size < 32 is not allowed
        abort();
    else if ((ppBlock->header & ~(0x7)) % 8 != 0) // block size has to be a multiple of 8
        abort();
    else if ((void *)ppBlock < block_start)
        abort();
    else if (ppEnd > block_end)
        abort();
    else if ((ppBlock->header & (1 << 0)) == 0) // alloc = 0
        abort();
    else if ((ppBlock->header & (1 << 2)) != 0) // in quick list = 1
        abort();
    else if ((ppBlock->header & (1 << 1)) == 0 && ((*prevFooter) & (1 << 0)) == 1) // alloc and prev alloc bits don't match
        abort();

    // calculate index in quick list and try to insert
    int quick_index = ((ppBlock->header & ~(0x7)) - 32) / 8;

    if (quick_index >= 0 && quick_index < NUM_QUICK_LISTS)
    {
        // call insert quick list function
        quick_list_insert(quick_index, ppBlock);
    }

    else
    {
        // else if block is too large for a quick list position, coalesce with previous and next block if possible
        sf_block *coal_block = coalesce_prev(ppBlock);
        coal_block = coalesce_next(coal_block); // coalesces take care of free list insertion
    }
}

//----------------------------------------------- realloc -------------------------------------------------------

void *sf_realloc(void *pp, size_t rsize)
{
    sf_block *ppBlock = (sf_block *)(pp - 8); // find the actual block position
    void *block_start = sf_mem_start() + 32;  // find block start/end address
    void *block_end = sf_mem_end() - 8;
    void *ppEnd = (void *)ppBlock + (int)(ppBlock->header & ~(0x7)); // find end address of give block
    sf_footer *prevFooter = (sf_footer *)(pp - 16);                  // get prev block footer position

    // ----------verify pointer validity------------
    if (pp == NULL){ // pointer cannot be NULL
        errno=EINVAL;
        return NULL;
    }
    else if (((uintptr_t)pp & 0x7) != 0){ // payload 8 bytes aligned means header 8 bytes aligned
        errno=EINVAL;
        return NULL;
    }
    else if ((ppBlock->header & ~(0x7)) < 32){ // block size < 32 is not allowed
        errno=EINVAL;
        return NULL;
    }
    else if ((ppBlock->header & ~(0x7)) % 8 != 0){ // block size has to be a multiple of 8
        errno=EINVAL;
        return NULL;
    }
    else if ((void *)ppBlock < block_start){
        errno=EINVAL;
        return NULL;
    }
    else if (ppEnd > block_end){
        errno=EINVAL;
        return NULL;
    }
    else if ((ppBlock->header & (1 << 0)) == 0){ // alloc = 0
        errno=EINVAL;
        return NULL;
    }
    else if ((ppBlock->header & (1 << 2)) != 0){ // in quick list = 1
        errno=EINVAL;
        return NULL;
    }
    else if ((ppBlock->header & (1 << 1)) == 0 && ((*prevFooter) & (1 << 0)) == 1){  // alloc and prev alloc bits don't match
        errno=EINVAL;
        return NULL;
    }

    else if(rsize==0){        // if ptr valid but size = 0, call free, return NULL;
        sf_free(pp);
        return NULL;
    }

    int ppBlock_size=ppBlock->header & ~(0x7);            // get current block size
debug("pp block size: %d", ppBlock_size);
    if((int)rsize >ppBlock_size){                          // situation 1: allocate a larger size

        debug("\n------------------start allocating larger block-----------------\n");
        sf_block *newBlock=sf_malloc(rsize)-8;

debug("%p\n", newBlock);
debug("%d\n", (int)(newBlock->header & ~(0x7)));

        if(newBlock==NULL)
            return NULL;

        memcpy(newBlock->body.payload, ppBlock->body.payload, (size_t)ppBlock_size);                 // copy the data in input block to new block

        sf_free(pp);

debug("see the final heap\n\n");
// sf_show_heap();

        return newBlock->body.payload;
    }

    else if((int)rsize<ppBlock_size){                       // situation 2: split to a smaller size
        sf_block *newBlock = split(rsize, ppBlock);            // split

        sf_block *nextBlock=(sf_block*)((void *)newBlock+ (newBlock->header & ~(0x7)) );
        nextBlock=coalesce_next(nextBlock);

debug("address of next block: %p\n", nextBlock);
// sf_show_heap();
        return newBlock->body.payload;
    }
    return ppBlock->body.payload;
}

//----------------------------------------------- memalign -------------------------------------------------------
void *sf_memalign(size_t size, size_t align)
{
    int prevBlockSize=0;
    int newSize=0;

    if((int)align %2 != 0 || (int)align<8){              // verify align validity
        sf_errno=EINVAL;
        return NULL;
    }
    else if(size==0)                                        // verify size validity 
        return NULL;

    // calculate new size of a large enough block 
    newSize=size+align+32+8+8;

    // if(size<24){
    //     newSize=24+align+32;
    // }
    // else if(size>=24){
    //     newSize=size+align+32;
    // }

    debug("%d\n", (int)newSize);

    sf_block *largeBlock=(sf_block*)sf_malloc(newSize);     // malloc returns the payload of the large block, which is declared as a new block
    
    if(largeBlock==NULL){
        return NULL;
    }
    
    sf_block *prevBlock=(sf_block*)((void*)largeBlock-8);                       // find the address of the prev block 
    newSize=prevBlock->header & ~(0x7);                     // get new size in the header

// debug("%p\n",prevBlock);
// debug("%p\n",largeBlock);
// debug("new size now: %d\n",(int)newSize);
// debug("previous block size: %d\n",(int)((void*)largeBlock-(void*)prevBlock));

    while(((uintptr_t)((void*)largeBlock+8) % align) !=0 || prevBlockSize<32){               // while not finding an address of correct alignment,
        largeBlock=(sf_block*)((void*)largeBlock+8);                                         // keep move down by 8 each time until one is present
        prevBlockSize=(int)((void*)largeBlock-(void*)prevBlock);                             // get the size of the prev block   

debug("previous block size: %d\n",prevBlockSize);
debug("%s\n", ((uintptr_t)((void*)largeBlock+8) % align)==0? "alignment fullfilled":"not aligned");
    }

// debug("\n%p\n",largeBlock);
// debug("%s\n", ((uintptr_t)((void*)largeBlock+8) % align)==0? "32 byte aligned":"not 32 byte aligned");
// debug("previous block size: %d\n", prevBlockSize);

    // calculate the new prevblock header, coalesce and free it
    prevBlock->header= prevBlock->header-(prevBlock->header & ~(0x7))+ prevBlockSize;

    int restSize=newSize-prevBlockSize;                      // get the size of the rest of the blocks
    largeBlock->header=restSize;
    largeBlock->header |= 1 << 0;                            // set the alloc bit of largeBlock = 1

debug("large block header: %d\n",(int)largeBlock->header & ~(0x7));
debug("size: %d\n", (int)size);

    sf_block* temp=coalesce_prev(prevBlock);                // coalesce prev block and free it
    temp=split(size, largeBlock);                           // split the block in the end and free 

    sf_block *nextBlock=(sf_block*)((void*)temp+(int)(temp->header & ~(0x7)));
    free_list_takeout(nextBlock->header & ~(0x7), nextBlock);              // take out next block which is placed into free list by split
    coalesce_next(nextBlock);                                              // coalesce it with preceding block 

debug("\n-------------show heap at last-------------\n");
// sf_show_heap();
    return temp->body.payload;
}
