// AviumUI minimal 802.11 raw injector for monitor-iface testing.
// Sends N radiotap+802.11 deauth frames out an AF_PACKET socket on <iface>.
// Usage: mon_inject <iface> <count> [a2_mac aa:bb:..] [a1_mac]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <stdint.h>

static int parse_mac(const char *s, uint8_t *m){
    return sscanf(s,"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &m[0],&m[1],&m[2],&m[3],&m[4],&m[5])==6;
}

int main(int argc,char**argv){
    const char*ifn = argc>1?argv[1]:"wlan0";
    int n = argc>2?atoi(argv[2]):10;
    uint8_t a1[6]={0xff,0xff,0xff,0xff,0xff,0xff};            // dest (bcast)
    uint8_t a2[6]={0x02,0x11,0x22,0x33,0x44,0x55};            // src/BSSID
    if(argc>3) parse_mac(argv[3],a2);
    if(argc>4) parse_mac(argv[4],a1);

    int s=socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
    if(s<0){perror("socket");return 1;}
    /* bypass qdisc -> dev_hard_start_xmit directly -> ndo_start_xmit */
    int one=1;
#ifndef SOL_PACKET
#define SOL_PACKET 263
#endif
#ifndef PACKET_QDISC_BYPASS
#define PACKET_QDISC_BYPASS 20
#endif
    if(setsockopt(s,SOL_PACKET,PACKET_QDISC_BYPASS,&one,sizeof one)<0)
        perror("PACKET_QDISC_BYPASS");
    else
        printf("PACKET_QDISC_BYPASS set\n");
    struct ifreq ifr; memset(&ifr,0,sizeof ifr);
    strncpy(ifr.ifr_name,ifn,IFNAMSIZ-1);
    if(ioctl(s,SIOCGIFINDEX,&ifr)<0){perror("SIOCGIFINDEX");return 1;}
    struct sockaddr_ll sll; memset(&sll,0,sizeof sll);
    sll.sll_family=AF_PACKET; sll.sll_ifindex=ifr.ifr_ifindex;
    sll.sll_halen=6; memcpy(sll.sll_addr,a1,6);

    // radiotap: version0,pad0,len=8,present=0
    uint8_t rtap[8]={0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00};
    // deauth mgmt frame (fc=0xC0), reason 7
    uint8_t mac80211[26]={0xc0,0x00, 0x00,0x00,
        a1[0],a1[1],a1[2],a1[3],a1[4],a1[5],
        a2[0],a2[1],a2[2],a2[3],a2[4],a2[5],
        a2[0],a2[1],a2[2],a2[3],a2[4],a2[5],
        0x00,0x00, 0x07,0x00};
    uint8_t frame[8+26]; memcpy(frame,rtap,8); memcpy(frame+8,mac80211,26);

    int ok=0;
    for(int i=0;i<n;i++){
        ssize_t r=sendto(s,frame,sizeof frame,0,
                         (struct sockaddr*)&sll,sizeof sll);
        if(r<0){ if(i==0) perror("sendto"); }
        else ok++;
        usleep(50000);
    }
    printf("sent %d/%d frames on %s (len=%zu each)\n",ok,n,ifn,sizeof frame);
    return 0;
}
