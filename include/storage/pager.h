#pragma once
#include<string>
#include<cstdint>
#include<stdexcept>
using namespace std;

constexpr size_t PAGE_SIZE=4096;
using PageId=int32_t;

class Pager{
public:
     //Constructor to read file
     Pager(const string& file);
     //Destructor
     ~Pager();

     //preventting the copying by constructor
     Pager(const Pager&)=delete;
     Pager& operator=(const Pager&)=delete;

     //IO operations
     void ReadPage(PageId page_id,char* pagedata);
     void WritePage(PageId page_id,const char* pagedata);

     //Exanding the file and getting new pageid
     PageId AllocatePage();

     //forcing OS to flush hardware buffers to physical disk
     void Sync();

private:
     int fd_;
     size_t file_size_;
     PageId next_page_id_;
};