#include "storage/pager.h"
#include<fcntl.h>
#include<unistd.h>
#include<sys/stat.h>
#include<cstring>
#include<iostream>

Pager::Pager(const string& filename){
    //opening the file
    //O_RDWR:open for reading and writing and O+CREAT:create the file if not exist
    //0666:file permission 
    fd_=open(filename.c_str(),O_RDWR|O_CREAT,0666);
    if(fd_==-1){
        throw runtime_error("Failed to open database file");
    }

    //filesize
    struct stat file_stat;
    if(fstat(fd_,&file_stat)==-1){
        throw runtime_error("failed to get file stats");
    }

    file_size_=file_stat.st_size;
    next_page_id_=file_size_/PAGE_SIZE;
}

Pager::~Pager(){
    if(fd_!=-1){
        close(fd_);
    }
}


void Pager::ReadPage(PageId page_id,char* pagedata){
    if(page_id>=next_page_id_){
        throw runtime_error("Attempted to read Invalid page id");
    }

    //claculate where the page start in the file
    off_t offset=page_id*PAGE_SIZE;

    //file reading using pread ,which is thread safe and doesn't change the file pointer
    ssize_t bytes_read=pread(fd_,pagedata,PAGE_SIZE,offset);
    if(bytes_read==-1){
        throw runtime_error("Failed to read page");
    }

    //if the page is partially filled, we need to fill the rest with zeros
    if(bytes_read<PAGE_SIZE){
        memset(pagedata+bytes_read,0,PAGE_SIZE-bytes_read); 
    }
}


void Pager::WritePage(PageId page_id,const char* pagedata){
    if(page_id>=next_page_id_){
        throw runtime_error("Attempted to write Invalid page id");
    }

    off_t offset=page_id*PAGE_SIZE;
    ssize_t bytes_written=pwrite(fd_,pagedata,PAGE_SIZE,offset);
    if(bytes_written==-1){
        throw runtime_error("Failed to write page");
    }
}

PageId Pager::AllocatePage(){
    PageId page_id=next_page_id_;
    next_page_id_++;

    file_size_+=PAGE_SIZE;

    return page_id;
}

void Pager::Sync(){
    //fsync forces the OS to flush hardware buffers to physical disk
    if(fsync(fd_)==-1){
        throw runtime_error("Failed to sync data to disk");
    }
}