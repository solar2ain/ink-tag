#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_resource_name.h"
int main(void) {
    const char *reserved[]={"CALENDAR","clock","DeViCe","SLIDESHOW","NEXT","PREVIOUS","CLEAR","日历","时间","设备","轮播","下一页","上一页","清屏","IKNC1:00000000","ikncAnything"};
    for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++)
        assert(!inktag_nfc_resource_alias_valid((const uint8_t *)reserved[i],strlen(reserved[i])));
    uint8_t text[48];
    for(unsigned n=0;n<1000;n++) {
        size_t size=(size_t)snprintf((char *)text,sizeof(text),"sLoT-%u",n);
        assert(inktag_nfc_resource_alias_valid(text,size)==(n==0||n>256));
    }
    memset(text,'x',sizeof(text));
    assert(inktag_nfc_resource_alias_valid(text,35));
    assert(!inktag_nfc_resource_alias_valid(text,36));
    assert(!inktag_nfc_resource_alias_valid(NULL,1));
    assert(!inktag_nfc_resource_alias_valid(text,0));
    const uint8_t bad[][4]={{0xc0,0xaf},{0xed,0xa0,0x80},{0xf4,0x90,0x80,0x80},{0xe2,0x82}};
    const unsigned lengths[]={2,3,4,2};
    for(unsigned i=0;i<4;i++)assert(!inktag_nfc_resource_alias_valid(bad[i],lengths[i]));
    const uint8_t name[]={0xe5,0x9b,0xbe,'A'};
    const uint8_t same[]={0xe5,0x9b,0xbe,'a'};
    assert(inktag_nfc_resource_alias_valid(name,sizeof(name)));
    assert(inktag_nfc_resource_alias_matches(same,sizeof(same),name,sizeof(name)));
    uint32_t random=1;
    for(unsigned i=0;i<50000;i++) {
        random=random*1664525u+1013904223u;size_t n=1+(random%35);
        for(size_t j=0;j<n;j++){random=random*1664525u+1013904223u;text[j]=(uint8_t)('a'+(random%26));}
        bool valid=inktag_nfc_resource_alias_valid(text,n);
        uint8_t candidate[48];memcpy(candidate,text,n);
        for(size_t j=0;j<n;j++)candidate[j]-=32;
        assert(inktag_nfc_resource_alias_matches(candidate,n,text,n)==valid);
        assert(!inktag_nfc_resource_alias_matches(candidate,n-1,text,n));
    }
    return 0;
}
