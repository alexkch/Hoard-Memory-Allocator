
// IMPLEENTING DOUBLE LINKED LIST FOR CSC469A2




/*
void transfer_sblock(heap * target_heap) {
    
	superblock *super_before_emptiest = NULL;
	uint32_t emptiest_size = 0;
	uint32_t emptiest_position = 0;

	for (int i = 0; i < SZCLASS; i++) {
		
		// Initialize counters
		int j = 0;
		superblock * superblock_i = target_heap->superblocks[i];
		
		// Superblock[i] is not empty
		if (!superblock_i) {

			// Check if emptiest is set
			if (super_before_emptiest == NULL) {
				super_before_emptiest = superblock_i;
				emptiest_size = i;
				emptiest_position = j;
			
			// If it is, check the first element of superblock[i]
			} else if (superblock_i->used < super_before_emptiest->used) {
				super_before_emptiest = superblock_i;
				emptiest_size = i;
				emptiest_position = j;
			}
			j++;

			// Iterate over every superblock
			while (superblock_i->next) {
				if (superblock_i->next->used == 0) {
					transfer_superblock(superblock_i, emptiest_size, emptiest_position);
					return;
				}
				if (superblock_i->next->used < super_before_emptiest->used) {
					super_before_emptiest = superblock_i;
					emptiest_size = i;
					emptiest_position = j;
				}

				// Increment
				superblock_i = superblock_i->next;
				j++;, int old, int new)
			}
		}
	}
	transfer_superblock(super_before_emptiest, emptiest_size, emptiest_position); 
	return;
}
*/

        
/*
void add_to_bin(int heap_id, superblock * candidate, int bin) {
 
    
    int size_class = PWR_TO_IDX(candidate->power);
    superblock * current = HEAPS[heap_id].superblocks[bin][size_class];
    superblock * prev = NULL;
   
  
    while (current){
        
        
        if (candidate->used < current->used) {
            
            if (current->left) {
            
                current->left->right = candidate;
                candidate->left = current->left;
                candidate->right = current;
                current->left = candidate;
                return;
            }
            else {
                
                current->left = candidate;
                candidate->right = current;
                candidate->left = NULL;
                HEAPS[heap_id].superblocks[bin][size_class] = candidate;
                return;
                
            }
        }
        prev = current;
        current = current->right;
    }
    
    if (prev) {
            
        if (candidate->used < prev->used) {
            candidate->left = prev->left;
            candidate->right = prev;
            prev->left = candidate;
            prev->right = NULL;
            HEAPS[heap_id].superblocks[bin][size_class] = candidate;
            return;
        }
        else {
        
            candidate->left = prev;
            candidate->right = NULL;
            prev->right = candidate;
            return;
            
        }
        
    }
    
    HEAPS[heap_id].superblocks[bin][size_class] = candidate;
    return;
}
*/
/*
void remove_from_bin(heap * currentheap, superblock * candidate, int bin) {

    int size_class = PWR_TO_IDX(candidate->power);

    if (!(candidate->left) && !(candidate->right)) {
    
        currentheap->superblocks[bin][size_class] = NULL;
        
    }
    else if (!(candidate->left)) {
        
        currentheap->superblocks[bin][size_class] = candidate->right;
        candidate->right->left = NULL;
        candidate->right = NULL;
        candidate->left = NULL;
        
    }   
        
    else if (!(candidate->right)) {
        
        candidate->left->right = NULL;
        candidate->left = NULL;
        candidate->right = NULL;
    }
        
    else { //has both right and left

        candidate->left->right = candidate->right;
        candidate->right->left = candidate->left;
        candidate->left = NULL;
        candidate->left = NULL;
        
    }
}
*/

/*
void relink_bin(heap * currentheap, superblock * candidate, int bin, int type) {
    
    int size_class = PWR_TO_IDX(candidate->power);
    
    if (type == 1) { //relink from malloc
        if (candidate->right) {
            
            if (candidate->used > candidate->right->used) {
                
                if (candidate->left) {
                    
                    candidate->left->right = candidate->right;
                    candidate->right->left = candidate->left;
                    candidate->left = candidate->right;
                    candidate->right = candidate->right->right;
                    candidate->left->right = candidate;
                    
                    if (candidate->right) {
                        candidate->right->left = candidate;
                    }
                    
                }
                else {
                    
                    currentheap->superblocks[bin][size_class] = candidate->right;                  
                    candidate->left = candidate->right;
                    candidate->right = candidate->right->right;
                    candidate->left->right = candidate;
                    candidate->left->left = NULL;
                    
                    if (candidate->right) {
                        candidate->right->left = candidate;
                    }
                }
            }
        }
    }
    else { //(type == 2)
        if (candidate->left) {
            
            if (candidate->used < candidate->left->used) {
                
                if (!(candidate->left->left)) {
                    currentheap->superblocks[bin][size_class] = candidate;  
                }
                
                if (candidate->right) {
                    
                    candidate->left->right = candidate->right;
                    candidate->right->left = candidate->left;
                    candidate->right = candidate->left;
                    candidate->left = candidate->left->left;
                    candidate->left->right = candidate;
                    
                    if (candidate->right) {
                        candidate->right->left = candidate;
                    }
                    
                }
                else {
                    
                    currentheap->superblocks[bin][size_class] = candidate->right;                  
                    candidate->left = candidate->right;
                    candidate->right = candidate->right->right;
                    candidate->left->right = candidate;
                    candidate->left->left = NULL;
                    
                    if (candidate->right) {
                        candidate->right->left = candidate;
                    }
                }
            }
        }
    }
}
       */