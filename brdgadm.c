/*
 * Copyright (C) 2010 Kazuyoshi Aizawa. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/********************************************************************
 * brdgadm
 *
 * gcc brdgadm.c -lsocket -lnsl -o brdgadm 
 *
 * Command which configures brdg module
 *
 * Usage: 
 *   brdgadm -a interface    # Add interface as switch port
 *   brdgadm -d interface    # Delete interface
 *
 *********************************************************************/
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stropts.h>
#include <errno.h>
#include <sys/dlpi.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/signal.h>
#include <sys/stream.h>
#include <string.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in_systm.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <stdlib.h>
#include <sys/varargs.h>
#include <strings.h>
#include <ctype.h>

#define MAXDLBUF        32768
#define MUXIDFILE        "/tmp/brdg.muxid" /* File that stores mux_id*/

int add_interface(char *);
int delete_interface(char *);
int list_interface();
int print_usage(char *);

extern int dlattachreq(int, t_uscalar_t, caddr_t );
extern int dlpromisconreq(int, t_uscalar_t, caddr_t);
extern int dlbindreq(int, t_uscalar_t, t_uscalar_t, uint16_t, uint16_t, t_uscalar_t, caddr_t);
extern int dldetachreq(int , caddr_t);
extern int dlpromiscoffreq(int, t_uscalar_t, caddr_t);
extern int strioctl(int , int , int , int , char *);

int
main(int argc, char *argv[])
{
    int     i;
    extern char *optarg;

    if( argc == 1 )
        list_interface();

    if ( getuid() != 0){
        fprintf(stderr, "Permission denied\n");
        exit(1);
    }
    
    while ((i = getopt (argc, argv, "d:a:l")) != EOF) {
        switch (i){
            case 'd':
                delete_interface(optarg);                
                break;
            case 'a':
                add_interface(optarg);
                break;
            case 'l':
                list_interface();
                break;                
            default:
                print_usage(argv[0]);
                break;
        }
    }
    exit(0);
}

int
print_usage(char *argv)
{
    printf("Usage: %s [ -a interface | -d interface] \n",argv);
    printf("Options:\n");
    printf(" -a interface\t: Add interface as port\n");
    printf(" -d interface\t: Delete interface from port list\n");
    printf(" -l \t\t: List all interfaces in port list\n");    
    exit(1);
}

/*******************************************************
 * delete_interface()
 *
 * Delete interface from port list.
 * Delete entry from /tmp/brdg.muxid file.
 * 
 *  Arguments:
 *          interface : network interface name
 *  Return:
 *           int
 ******************************************************/
int
delete_interface(char *interface)
{
    int     ip_fd;
    int     muxid = 0; 
    FILE    *fp;
    char    entry[128];
    char    *p;
    char    backup[1024];
    
    bzero(entry, 128);
    bzero(backup, 1024);
    
    if ((fp = fopen(MUXIDFILE, "r")) == NULL) {
        fprintf(stderr,"Can't open %s\n", MUXIDFILE);
        fclose(fp);        
        exit(1);
    } 
    /*
     * Search interface entry within /tmp/brdg.muxid file.
     */
    while (fgets(entry, sizeof(entry), fp) != NULL){
        if (strncmp(entry, interface, strlen(interface)) == 0){
            p = strpbrk(entry, ":");                
            muxid = atoi(p+1);
        } else {
            strcat(backup, entry);
        }
    }        
    if (muxid == 0){
        fprintf(stderr,"Interface %s is not registerd\n",interface );
        fclose(fp);
        exit(1);
    }
    fclose(fp);

    if ((fp = fopen(MUXIDFILE, "w")) == NULL) {
        fprintf(stderr,"Can't open %s\n", MUXIDFILE);
        fclose(fp);        
        exit(1);
    }
    fputs(backup,fp);
    fclose(fp);
    
    if ((ip_fd = open("/dev/ip", O_RDWR)) < 0){
        perror("/dev/ip");
        exit(0);
    }    

    if (ioctl(ip_fd, I_PUNLINK, muxid) < 0) {
        printf("failed to unlink\n");
        exit(1);
    }
    printf("%s successfully deleted.\n", interface);
    exit(0);
}

/*******************************************************
 * add_interface()
 *
 * Add network interface as a port.
 * Store network interface name and MUX ID in /tmp/brdg.muxid
 * file for later deletion.
 * 
 *  Arguments:
 *          interface : network interface name 
 *  Return:
 *           int
 ******************************************************/
int add_interface(interface)
char        *interface;     /* interface name */
{
    int       i;    
    char      buf[MAXDLBUF];    
    uint32_t  ppa = 0;      /* PPA(Physical Point of address). */
    uint32_t  if_fd, ip_fd; /* FD# for IP driver. And FD# for interface driver */
    uint32_t  muxid;        /* Multiplexer ID */
    char      devname[30];  /* interface name without instance number (hme)*/
    char      devpath[30];  /* Path to the device (/dev/hme)*/
    FILE      *fp;          /* File pointer of /tmp/brdg.muxid */
    char      entry[30];
    uint32_t  ifnamelen;    /* Length of interface name */
    char      *tempchar;
    
    /*
     * Check /tmp/brdg.muxid 
     */
    if ((fp = fopen(MUXIDFILE, "r")) != NULL) {
        while (fgets(entry, sizeof(entry), fp) != NULL){
            if (strncmp(entry, interface, strlen(interface)) == 0){
                fprintf(stderr, "Interface %s is already registerd. Please remove first\n", interface);
                exit(1);
            }
        }
        fclose(fp);
    }

    ifnamelen = strlen(interface);
    tempchar = interface;
    
    if ( isdigit ((int) tempchar[ifnamelen - 1]) == 0 ){
        /* interface doesn't have instance number */
        fprintf(stderr, "Please specify instance number (e.g. hme0, eri2)\n");
        exit(1);
    }
    
    for ( i = ifnamelen - 2 ; i >= 0 ; i--){
        if ( isdigit ((int) tempchar[i]) == 0 ){
            ppa = atoi(&(tempchar[i + 1]));
            break;
        }
        if (i == 0) {
            /* looks all char are digit.. can't handle */
            fprintf(stderr, "Invalid interface name\n");
            exit(1);
        }
        continue;
    }
    strlcpy(devname, interface, i + 2);

    sprintf(devpath, "/dev/%s",devname);
    //printf("devname = %s, ppa = %d devpath = %s \n",devname, ppa, devpath);

    /*
     * Open /dev/ip and network interface
     */
    if ((ip_fd = open("/dev/ip", O_RDWR)) < 0){
        perror("/dev/ip");
        exit(1);
    }

    if((if_fd = open (devpath , O_RDWR)) < 0 ){
        perror(devpath);
        exit(1);
    }

    /*
     * Attach request.
     */
    if( dlattachreq(if_fd, ppa, buf) < 0)
        exit(1);
        
    /*
     * Bind request. 
     */
    if( dlbindreq (if_fd, 0, 0, DL_CLDLS, 0, 0, buf) < 0)
        exit(1);
             
    /*
     * Set PROMISCOUS mode.
     */
    if(dlpromisconreq(if_fd, DL_PROMISC_SAP, buf) < 0)
        exit(1);
    if(dlpromisconreq(if_fd, DL_PROMISC_PHYS, buf) < 0)
        exit(1);

    if (strioctl(if_fd, DLIOCRAW, -1, 0, NULL) < 0){
        perror("DLIOCRAW");
        exit(1);
    }

    /*
     * Flush Queue
     */
    if (ioctl(if_fd, I_FLUSH, FLUSHR) < 0){
        perror("I_FLUSH");
        exit(1);
    }

    /*
     * Push brdg module into interface's stream.
     */
    if (ioctl(if_fd, I_PUSH, "brdg") < 0){
        perror("I_PUSH");
        exit(1);
    }

    /*
     * Link inteface's stream to ip's stream.
     * (PLINK = persist link)
     */
    muxid = ioctl(ip_fd, I_PLINK, if_fd);
    if( muxid < 0){
        perror("I_PLINK ioctl failed");
        exit(1);
    }
    
    if ((fp = fopen(MUXIDFILE, "a")) == NULL) {
        fprintf(stderr,"Can't open %s\n", MUXIDFILE);
        exit(1);	
    }

    sprintf(entry, "%s:%d\n", interface, muxid);
    fputs(entry,fp);
    fclose(fp);
    
    printf("%s successfully added.\n", interface);
    close(ip_fd);
    close(if_fd);
    exit(0);
}

/***************************************************************
 * list_interface()
 *
 * List network intefaces which have already been registered.
 ***************************************************************/
int list_interface()
{
    FILE    *fp;
    char    entry[128];
    
    if ((fp = fopen(MUXIDFILE, "r")) == NULL) {
        if (errno == ENOENT)
            exit(0);
        fprintf(stderr,"Can't open %s\n", MUXIDFILE);
        fclose(fp);        
        exit(1);
    } 
    printf("List of the interfaces\n");
    printf("----------\n");        
    while (fgets(entry, sizeof(entry), fp) != NULL){
        printf("%s\n", strtok(entry, ":"));
    }
    exit(0);
}
