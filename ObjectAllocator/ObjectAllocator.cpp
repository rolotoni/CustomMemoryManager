/******************************************************************************/
/*!
\file   ObjectAllocator.cpp
\author Jesse Harrison
\par    email: jesse.harrison\@digipen.edu
\date   1/26/2011
\brief
    This file contains the implementation of the following functions for
    the memory manager assignment.
    
    Functions include:
    - Constructor          
    - Allocate
    - Free
    - ImplementedExtraCredit *NOT IMPLEMENTED*
    - DumpMemoryInUse
    - ValidatePages
    - FreeEmptyPages *NOT IMPLEMENTED*
    - SetDebugState
    - GetFreeList
    - GetPageList
    - GetConfig
    - GetStats
    - DeAllocatePages
    - AllocatePage
    - ValidateObject
    - ValidateBlock
    - SetSignatures



*/
/******************************************************************************/

#include "ObjectAllocator.h"

/******************************************************************************/
/*!
      \brief
       Constructor for the ObjectAllocator class
      
      \param ObjectSize
        Size of each object on a page
      
      \param config
        the specifications of the config struct Objects per page, max pages, etc..

*/
/******************************************************************************/
ObjectAllocator::ObjectAllocator(unsigned ObjectSize, const OAConfig& config)throw(OAException)
{ 
   //Initialize each objects size and the config struct
   OAStats_.ObjectSize_ = ObjectSize;
   Config_.UseCPPMemManager_ = config.UseCPPMemManager_; 
   Config_.ObjectsPerPage_   = config.ObjectsPerPage_; 
   Config_.MaxPages_ = config.MaxPages_; 
   Config_.DebugOn_  = config.DebugOn_;
   Config_.PadBytes_ = config.PadBytes_;
   Config_.HeaderBlocks_ = config.HeaderBlocks_;
   Config_.Alignment_ = config.Alignment_;
   
   chunk_size_ = (Config_.PadBytes_ * 2) + Config_.HeaderBlocks_ + Config_.Alignment_;   
   block_size_ = OAStats_.ObjectSize_ + chunk_size_;   
   
   //set page and free list to null
   page_list_ = NULL;
   free_list_ = NULL;
   
   
   //allocate first page of memory for client
   if(!Config_.UseCPPMemManager_)
     AllocatePage();
   
   Config_.LeftAlignSize_ = Config_.Alignment_;  // number of alignment bytes required to align first block
   Config_.InterAlignSize_ = Config_.Alignment_; // number of alignment bytes required between remaining blocks

}

/******************************************************************************/
/*!
      \brief
       Destructor for the ObjectAllocator class
       deletes all memory allocated

*/
/******************************************************************************/
ObjectAllocator::~ObjectAllocator() throw()
{
  if(!Config_.UseCPPMemManager_)
    DeAllocatePages(); // delete all memory allocated
}

/******************************************************************************/
/*!
      \brief
       Allocates a block of memory for the client and
       returns to them a pointer to the block 
      
      \return
        a pointer to the block of memory allocated
      
*/
/******************************************************************************/
void* ObjectAllocator::Allocate() throw(OAException)
{
   
   //allocator disabled
   //allocate using new
    if(Config_.UseCPPMemManager_)
    {
      char* new_mem = new char[OAStats_.ObjectSize_];
      --OAStats_.FreeObjects_;
      ++OAStats_.ObjectsInUse_;
      ++OAStats_.Allocations_;

      if(OAStats_.MostObjects_ < OAStats_.ObjectsInUse_)
        ++OAStats_.MostObjects_;

      return new_mem;
    }
   //if there are no more free objects
   //need to allocate new page
   if(OAStats_.FreeObjects_ == 0 )
   {
     //if we have reached our max amount of pages throw exception
     if(OAStats_.PagesInUse_ == Config_.MaxPages_)
       throw OAException(OAException::E_NO_PAGES, 
                         "allocate_new_page: The maximum number of pages has been allocated.");
     AllocatePage();
   }
    
    //set temp = freelist in order to swap pointers
   GenericObject* temp = free_list_;
   
   free_list_ = temp->Next;
   
   //set allocated signature if debugging
   if(Config_.DebugOn_)
   {
     char * set_sig = reinterpret_cast<char*>(temp);
     unsigned object = OAStats_.ObjectSize_;
     while(object--)
     {
       *set_sig = ALLOCATED_PATTERN;
       ++set_sig;
     }
   }
   
   //set header block to in use
   if(Config_.HeaderBlocks_)
   {
     char* temp_free = reinterpret_cast<char*>(temp);
     temp_free -= (Config_.PadBytes_ + 1);
     *temp_free = 1;    
   }
   
   //update stats
   --OAStats_.FreeObjects_;
   ++OAStats_.ObjectsInUse_;
   ++OAStats_.Allocations_;
     
   if(OAStats_.MostObjects_ < OAStats_.ObjectsInUse_)
     ++OAStats_.MostObjects_;
       
   return temp;
}

/******************************************************************************/
/*!
      \brief
       frees a block of memory
      
      \param Object
         a pointer to the object to be freed
              
*/
/******************************************************************************/
void ObjectAllocator::Free(void *Object) throw(OAException)
{

    //allocator disabled
    if(Config_.UseCPPMemManager_)
    {
      delete [] reinterpret_cast<char*>(Object);
      //update stats
      ++OAStats_.FreeObjects_;
      ++OAStats_.Deallocations_;
      --OAStats_.ObjectsInUse_;

      return;
    }
   //used to re-assign pointers
   GenericObject* temp = reinterpret_cast<GenericObject*> (Object);
   
   //make sure is on a page, good boundary, etc...
    if(Config_.DebugOn_)
    {
      ValidateObject(Object);
    }
   
   //set free signature if debugging
   if(Config_.DebugOn_)
   {
     char * set_sig = reinterpret_cast<char*>(temp);
     //skip next pointer
     set_sig += sizeof(void*);
     unsigned object = OAStats_.ObjectSize_ - sizeof(void*);
     while(object--)
     {
       *set_sig = FREED_PATTERN;
       ++set_sig;
     }
   }
   
   //set header block to not in use
   if(Config_.HeaderBlocks_)
   {
     char* temp_free = reinterpret_cast<char*>(temp);
     temp_free -= (Config_.PadBytes_ + 1);
     *temp_free = 0;    
   }
   
   //perform free and re-assign pointers
   //check if free list is empty
   if(!free_list_)
   {
     free_list_ = temp;
     free_list_->Next = NULL;
   }
   else
   {
     temp->Next = free_list_;
     free_list_ = temp;
   }
   
   //update stats
   ++OAStats_.FreeObjects_;
   ++OAStats_.Deallocations_;
   --OAStats_.ObjectsInUse_;
}

/******************************************************************************/
/*!
      \brief
        Calls the callback fn for each block still in use
        
      \param fn
        the callback function for each block in use
      
      \return
        the number of blocks in use by client
      
*/
/******************************************************************************/
unsigned ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK fn) const
{
    //how many objects are still in use
    unsigned int in_use = 0;
    bool being_used = true;
    //walk through each page and if the block
    //is not on the free_list its in use
    GenericObject* temp_page_list = page_list_;
    while(temp_page_list)
    {
    
      if(Config_.HeaderBlocks_)
      {
         //get temp to current page
         char* temp_block = reinterpret_cast<char*>(temp_page_list);

         
         for(unsigned i = 0; i < Config_.ObjectsPerPage_;++i)
         {
           //first block
           if(i == 0)
             temp_block += (sizeof(void*) + chunk_size_ - Config_.PadBytes_);
           else
             temp_block += block_size_;
            //check header block, if 1 then in use
            if(*(temp_block - Config_.PadBytes_ - 1) == 1)
            {
              ++in_use;
              fn(temp_block + Config_.PadBytes_, OAStats_.ObjectSize_);
            }
         }
      }
      else 
      {
        //get to first block on page
        char* temp_block = reinterpret_cast<char*>(temp_page_list);

        //walk to each block in pagelist and see if
        //its on the free list
        for(unsigned i = 0; i < Config_.ObjectsPerPage_;++i)
        {
          //first block
          if(i == 0)
            temp_block += (sizeof(void*) + chunk_size_ - Config_.PadBytes_);
          else
            temp_block += block_size_;

          GenericObject* t_block = reinterpret_cast<GenericObject*>(temp_block);
          //walk the free list to see if the pointer
          //matches one of those if not it is in use
          GenericObject* temp_free = free_list_;
          while(temp_free)
          {
            //its on the free list break and do nothing
            if(temp_free == t_block)
              being_used = false;
       
             temp_free = temp_free->Next;
         
          }
          if(being_used)
          {
            ++in_use;
            unsigned char* temp_send = reinterpret_cast<unsigned char*>(t_block);
            fn(temp_send, OAStats_.ObjectSize_);
          }
		  
		    being_used = true;
         }
       }

      temp_page_list = temp_page_list->Next;
    }
	
   return in_use;
}
/******************************************************************************/
/*!
      \brief
        Calls the callback fn for each block that is potentially corrupted
        
      \param fn
        the callback function for each block that might be corrupted
      
      \return
        the number of blocks that are corrupted
      
*/
/******************************************************************************/
unsigned ObjectAllocator::ValidatePages(VALIDATECALLBACK fn) const
{
    unsigned corruptions = 0;
   //go through each page and check each blocks pad bytes
   //to see if they are overwritten if so pass back the * to the block
   //that is corrupted and the size of the object
   unsigned char * block;
   GenericObject* temp_page_list = page_list_;
   while(temp_page_list)
   {
     block = reinterpret_cast<unsigned char*>(temp_page_list);
     //go to first block
     block += (Config_.LeftAlignSize_ + Config_.HeaderBlocks_ + Config_.PadBytes_ + sizeof(void*));
     if(!ValidateBlock(block))
     {
       corruptions++;
       fn(block, OAStats_.ObjectSize_);
     }
   
     //do the rest of the blocks on the page
     for(unsigned i = 0; i < Config_.ObjectsPerPage_ - 1; ++i)
     {
       block += block_size_;
       if(!ValidateBlock(block))
       {
         corruptions++;
         fn(block, OAStats_.ObjectSize_);
       }
     }
   
     //go to the next page
     temp_page_list = temp_page_list->Next;
   }
   
   return corruptions;
}
/******************************************************************************/
/*!
      \brief
        Frees all empty pages *NOT IMPLEMENTED*
      
      \return
        the number of freed pages
      
*/
/******************************************************************************/
unsigned ObjectAllocator::FreeEmptyPages(void)
{
  return 1;
}
/******************************************************************************/
/*!
      \brief
        FReturns true if FreeEmptyPages and alignments 
                           are implemented *NOT IMPLEMENTED*
      
      \return
        If extra credit was done or not
      
*/
/******************************************************************************/
bool ObjectAllocator::ImplementedExtraCredit(void)
{
   return false;
}
/******************************************************************************/
/*!
      \brief
        Testing/Debugging/Statistic methods
      
      \param State
        If in debug mode or not
      
*/
/******************************************************************************/
void ObjectAllocator::SetDebugState(bool State) 
{
  Config_.DebugOn_ = State;
}
/******************************************************************************/
/*!
      \brief
        returns a pointer to the internal free list
      
      \return
        pointer to the free list
      
*/
/******************************************************************************/
const void* ObjectAllocator::GetFreeList(void) const
{
  return free_list_;
}
/******************************************************************************/
/*!
      \brief
        returns a pointer to the internal page list
      
      \return
        pointer to the page list
      
*/
/******************************************************************************/
const void* ObjectAllocator::GetPageList(void) const
{
  return page_list_;
} 
/******************************************************************************/
/*!
      \brief
        returns the configuration parameters
      
      \return
        config parameters
      
*/
/******************************************************************************/ 
OAConfig ObjectAllocator::GetConfig(void) const
{
  return Config_;
}
/******************************************************************************/
/*!
      \brief
        returns the statistics for the allocator
      
      \return
        Allocator stats
      
*/
/******************************************************************************/     
OAStats ObjectAllocator::GetStats(void) const
{
  return OAStats_;
}
/******************************************************************************/
/*!
      \brief
        Allocates and sets up the freelist for an entire page
      
*/
/******************************************************************************/          
void ObjectAllocator::AllocatePage()
{
  // size of a page: ObjectsPerPage_ * ObjectSize_ + sizeof(void*)
  OAStats_.PageSize_ = Config_.ObjectsPerPage_ * OAStats_.ObjectSize_ + sizeof(void*) 
                                                    + Config_.ObjectsPerPage_ * chunk_size_;
  
  ++OAStats_.PagesInUse_;
  //retrieve the chunk of memory from os aka allocate page
  //if new fails throw an exception
  char* NewPage = new (std::nothrow) char[OAStats_.PageSize_];
  if(!NewPage)
    throw OAException(OAException::E_NO_MEMORY, "allocate_new_page: No system memory available."); 
   
  //set the initial signatures for the page
  char* set_signatures = NewPage;
  SetSignatures(set_signatures);
   
   
  //cast page to generic object
  GenericObject* Page = reinterpret_cast<GenericObject*>(NewPage);
   
  //set the next pointer of page to previous page or NULL if first page
  if(OAStats_.PagesInUse_ == 1)
    Page->Next = NULL;
  else
    Page->Next = page_list_;
   
   //point pagelist to the beginning of the page
  page_list_ = Page; 
   
  char* temp_page_list = reinterpret_cast<char*>(page_list_);
  //use to walk through memory and set up page  
  //the start of the free_list_ begins after
  //the page_list_ pointer, alignment, header, padding 
  char* temp_free_list = reinterpret_cast<char*>(temp_page_list)
                         + sizeof(void*) + (chunk_size_ - Config_.PadBytes_);
                        //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^need to think about alignment
   
  free_list_ = reinterpret_cast<GenericObject*>(temp_free_list);
   
  GenericObject* Block = free_list_;
   
  Block->Next = NULL;
   
  //size of bytes in use blocks created
  unsigned commited_bytes = 0;

  //loop through page and set up free_list_
  for(unsigned i = 0; i < Config_.ObjectsPerPage_
               || commited_bytes >= OAStats_.PageSize_; ++i)
  {
    //avoid access overrun
    if(i < Config_.ObjectsPerPage_ - 1)
    {
      temp_free_list = reinterpret_cast<char*>(temp_free_list);
        
      temp_free_list += (OAStats_.ObjectSize_ + chunk_size_);
         
      GenericObject* block_temp = reinterpret_cast<GenericObject*>(temp_free_list);
      block_temp->Next = free_list_;
      free_list_ = reinterpret_cast<GenericObject*>(temp_free_list);
    }
	
     commited_bytes += OAStats_.ObjectSize_;
     ++OAStats_.FreeObjects_;
  }
   


   
}

/******************************************************************************/
/*!
      \brief
        frees all memory in use
      
*/
/******************************************************************************/ 
void ObjectAllocator::DeAllocatePages()
{
  char* temp;
  while(page_list_)
  {
    temp = reinterpret_cast<char *>(page_list_->Next);
    delete [] page_list_;
    page_list_ = reinterpret_cast<GenericObject*>(temp);
  }
}
/******************************************************************************/
/*!
      \brief
        Helper Function for Free.
        Validates a free requests from the client, makes sure they are
        not trying to double free, free on bad boundary etc.
      
      \param Object
        the requested object to be freed
      
*/
/******************************************************************************/ 
void ObjectAllocator::ValidateObject(void* Object)
{
   //used to re-assign pointers
   GenericObject* temp = reinterpret_cast<GenericObject*> (Object);
   GenericObject* temp_walk;
   //check multiple free via header block
   if(Config_.HeaderBlocks_)
   {
     unsigned char* header_check = reinterpret_cast<unsigned char*>(temp);
     if(*(header_check - Config_.PadBytes_) == 0)
       throw OAException(OAException::E_MULTIPLE_FREE,
                               "FreeObject: Object has already been freed.");
   }
   else
   {
     //perform check to see if the object has already
     //been freed by walking the free list
     temp_walk = free_list_;
   
     while(temp_walk != NULL)
     {
       //object on free_list_
       if(temp_walk == temp)
         throw OAException(OAException::E_MULTIPLE_FREE,
                               "FreeObject: Object has already been freed.");
        
       temp_walk = temp_walk->Next;
     }
   }
  
   
   //perform check to make sure the object trying to 
   //to be freed is on one of the pages
   
   temp_walk = page_list_;
   //if there are no pages then no memory can
   //be freed
   if(!page_list_)
     throw OAException(OAException::E_BAD_ADDRESS, "validate_object: Object not on a page.");
     
   //check address boundaries of each page
   //if not in boundaries cannot be freed
   unsigned page = 1;
   while(temp_walk)
   {
     char* temp_end = reinterpret_cast<char*>(temp_walk);
     temp_end += OAStats_.PageSize_;
     GenericObject* end_of_page = reinterpret_cast<GenericObject*>(temp_end);
     if(temp > temp_walk && temp < end_of_page)
       break;
     else if (page == OAStats_.PagesInUse_)
       throw OAException(OAException::E_BAD_ADDRESS, "validate_object: Object not on a page.");
     
     temp_walk = temp_walk->Next;
     ++page;
   }   
   
   //check to see if on a bad boundary
   //get to first block on the page
   unsigned char* block = reinterpret_cast<unsigned char*>(temp_walk);
   block += (sizeof(void*) + chunk_size_ - Config_.PadBytes_);
   
   unsigned char* free_block = reinterpret_cast<unsigned char*>(Object);
   
   //get the distance between two pointers
   unsigned distance = free_block - block;
   
   //remainder means bad boundary
   if(distance % (OAStats_.ObjectSize_ + chunk_size_))
     throw OAException(OAException::E_BAD_BOUNDARY,"validate_object: Object on bad boundary in page.");
     
   //test corruption
   //check left padding
   unsigned char* temp_free = free_block;

   //check padbytes to the left
   for(unsigned i = 0; i < Config_.PadBytes_; ++i)
   {
     //go back to pad byte closest to block
     --temp_free;
     //if pad pattern not in pad bytes
     //then block is corrupted
     if(*temp_free != PAD_PATTERN)
       throw OAException(OAException::E_CORRUPTED_BLOCK,"check_padbytes: Memory corrupted before block.");
        
   }
   
   //check pad bytes on the right
   temp_free = free_block;
   temp_free += OAStats_.ObjectSize_;
   
   for(unsigned i = 0; i < Config_.PadBytes_; ++i)
   {
     //if pad pattern not in pad bytes
     //then block is corrupted
     if(*temp_free != PAD_PATTERN)
      throw OAException(OAException::E_CORRUPTED_BLOCK,"check_padbytes: Memory corrupted after block.");

     temp_free++;
   }
   
}
/******************************************************************************/
/*!
      \brief
        Sets all Debug Signatures for an initial page and Header Blocks
        regardless of debug or not
      
      \param set_signatures
        the beginnig of where to put signatures
      
*/
/******************************************************************************/ 
void ObjectAllocator::SetSignatures(char * set_signatures)
{
  //set initial signatures
  //get past page list next pointer
  set_signatures += sizeof(void*);
  
  //set alignment if any
  if(Config_.DebugOn_)
  {
    if(Config_.LeftAlignSize_)
    {
      unsigned temp_align = Config_.Alignment_;
      while(temp_align--)
      {
        *set_signatures = ALIGN_PATTERN;
        ++set_signatures;
      }
     
    }
    //set header blocks if any
    if(Config_.HeaderBlocks_)
    {
      unsigned temp_header = Config_.HeaderBlocks_;
      while(temp_header--)
      {
        *set_signatures = 00;
        ++set_signatures;
      }
    }
  }
  else
  {
    set_signatures += Config_.LeftAlignSize_;
    //set header blocks if any
    if(Config_.HeaderBlocks_)
    {
      unsigned temp_header = Config_.HeaderBlocks_;
      while(temp_header--)
      {
        *set_signatures = 00;
        ++set_signatures;
      }
    }
  }
  if(Config_.DebugOn_)
  {
    //set pad bytes if any
    if(Config_.PadBytes_)
    {
      unsigned temp_pads = Config_.PadBytes_;
      while(temp_pads--)
      {
        *set_signatures = PAD_PATTERN;
        ++set_signatures;
      }
    }
  }
   
  //set each blocks signatures excluding
  //the last block, it only Unallocated and pad signatures
  //every block except for the last last
  for(unsigned i = 0; i < Config_.ObjectsPerPage_; ++i)
  {   
    if(Config_.DebugOn_)
    { 
      //skip next pointer at beginning of block
      set_signatures += sizeof(void*);
      //last block only do unallocated and padding at the end
       if(i == Config_.ObjectsPerPage_ - 1)
       {  
         unsigned temp_size = OAStats_.ObjectSize_ - sizeof(void*);
         while(temp_size--)
         {
           *set_signatures = UNALLOCATED_PATTERN;
           ++set_signatures;
         }
         //set pad bytes if any
         if(Config_.PadBytes_)
         {
           unsigned temp_pads = Config_.PadBytes_;
           while(temp_pads--)
           {
             *set_signatures = PAD_PATTERN;
             ++set_signatures;
           }
         }
       }
       else
       {
         unsigned temp_size = OAStats_.ObjectSize_ - sizeof(void*);
         while(temp_size--)
         {
           *set_signatures = UNALLOCATED_PATTERN;
           ++set_signatures;
         }
     
         //set pad bytes if any
         if(Config_.PadBytes_)
         {
           unsigned temp_pads = Config_.PadBytes_;
           while(temp_pads--)
           {
             *set_signatures = PAD_PATTERN;
             ++set_signatures;
           }
         }
     
        //set alignment if any
        if(Config_.InterAlignSize_)
        {
          unsigned temp_align = Config_.InterAlignSize_;
          while(temp_align--)
          {
            *set_signatures = ALIGN_PATTERN;
            ++set_signatures;
          }
     
        }
        //set header blocks if any
        if(Config_.HeaderBlocks_)
        {
          unsigned temp_header = Config_.HeaderBlocks_;
          while(temp_header--)
          {
            *set_signatures = 0;
            ++set_signatures;
          }
        }
        //set pad bytes if any
        if(Config_.PadBytes_)
        {
          unsigned temp_pads = Config_.PadBytes_;
          while(temp_pads--)
          {
            *set_signatures = PAD_PATTERN;
            ++set_signatures;
          }
        }
     
       }
     }
     else//just do headerblocks unless last block
     {
       //last block only do unallocated and padding at the end
       if(i == Config_.ObjectsPerPage_ - 1)
         break;

       set_signatures += (OAStats_.ObjectSize_ + Config_.InterAlignSize_ + Config_.PadBytes_);
       //set header blocks if any
       if(Config_.HeaderBlocks_)
       {
         unsigned temp_header = Config_.HeaderBlocks_;
         while(temp_header--)
         {
           *set_signatures = 0;
           ++set_signatures;
         }
       }
     }
     
  }
}

/******************************************************************************/
/*!
      \brief
         Helper Function for Validate Pages.
         Make sure a pad byte has not been overwitten on
         a given block
      
      \param block
        the block to check for corruption
        
      \return 
        false = block corrupted
        true = block valid
      
*/
/******************************************************************************/ 
bool ObjectAllocator::ValidateBlock(unsigned char* block) const
{
  unsigned char* temp_block = block;
   
  //check padbytes to the left
  for(unsigned i = 0; i < Config_.PadBytes_; ++i)
  {
    //go back to pad byte closest to block
    --temp_block;
    //if pad pattern not in pad bytes
    //then block is corrupted
    if(*temp_block != PAD_PATTERN)
    {
      return false;
    }
      
  }
   
  //check pad bytes on the right
  temp_block = block;
  temp_block += OAStats_.ObjectSize_;
   
   
  for(unsigned i = 0; i < Config_.PadBytes_; ++i)
  {
    //++temp_block;
    //if pad pattern not in pad bytes
    //then block is corrupted
    if(*temp_block++ != PAD_PATTERN)
    {
      return false;
    }
      
  }
   
   //no errors block validated
   return true;
   
} 