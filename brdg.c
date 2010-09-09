/*******************************************************
 * brdg.c
 * 
 * Bridge module for Solaris
 *
 * Copright (c) 2010  Kazuyoshi Aizawa <admin2@whiteboard.ne.jp> 
 * 
 * /usr/local/bin/gcc -D_KERNEL brdg.c -c
 * ld -dn -r brdg.o -o brdg
 *
 *******************************************************/

#include <netinet/in.h>
#include <inet/common.h>
#include <netinet/ip6.h>
#include <inet/ip.h>
#include <inet/tcp.h>
#include <sys/signal.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/dlpi.h>
#include <sys/ethernet.h>

#include <sys/modctl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>

#define  MAXPORT 20   /* Max number of ports to be bridged. (Max number of NICs)*/
#define  MAXHASH 1024 /* Max number of ethernet addresses to be registered. */
#define  MAX_MSG 256  /* Max length for syslog messages */

static int  brdg_open (queue_t*, dev_t*, int, int, cred_t*);
static int  brdg_close (queue_t*, int, int, cred_t*);
static int  brdg_wput (queue_t*, mblk_t*);
static int  brdg_rput (queue_t*, mblk_t*);
static int  brdg_rput_data (queue_t*, mblk_t*);
static void brdg_add_port (queue_t*, mblk_t*);
static void brdg_remove_port (queue_t*, mblk_t*);
static void brdg_register_node (queue_t *, mblk_t *);
static void debug_print (int , char *, ...);

/*
 * Port structure.
 * One port structure corresponds to one NIC added by brdgadm command.
 * It has an address of read side queue of brdg module which was PUSH'ed by brdgadm command.
 * 'MAXPORT' port structures are allocated when brdg module is loaded into the kernel
 * as a result of allocating port_list[] array.
 */
typedef struct port_s
{
    queue_t  *rqueue;   /* Read queue of brdg module which corresponds to this port.*/
    char     *ifname;   /* Not used. For future implementation */
    uint32_t muxid;     /* Not used. For future implementation */
} port_t;

port_t port_list[MAXPORT];

/*
 * Node structure.
 * Node structure corresponds to one source ethernet address.
 * This structure is allocated when receiving a packet.
 * It is intended to prevent forwarding a packet to which packet was received.
 */ 
typedef struct node_s
{
    struct    ether_addr ether_addr; /* Source ethenet address */
    port_t    *port;                 /* Port where this node is connected */
    uint16_t  state;
} node_t;

node_t node_hash_table[MAXHASH];

/*
 * Calculate a hash value from ethernet address [0-MAXHASH]
 */
#define ETHER_HASH(ether_addr) \
              (\
                   ((uint8_t)ether_addr.ether_addr_octet[0]    ) + \
                   ((uint8_t)ether_addr.ether_addr_octet[1]<<8 ) + \
                   ((uint8_t)ether_addr.ether_addr_octet[2]    ) + \
                   ((uint8_t)ether_addr.ether_addr_octet[3]<<8 ) + \
                   ((uint8_t)ether_addr.ether_addr_octet[4]    ) + \
                   ((uint8_t)ether_addr.ether_addr_octet[5]<<8 )   \
               ) % MAXHASH

/*
 * Debug routine.
 */
#ifdef DEBUG
#define DEBUG_PRINT_ETHER(str, ether_addr) \
            debug_print(CE_CONT,"%s %x:%x:%x:%x:%x:%x\n", \
                    str,\
                    (uint8_t)ether_addr.ether_addr_octet[0],\
                    (uint8_t)ether_addr.ether_addr_octet[1],\
                    (uint8_t)ether_addr.ether_addr_octet[2],\
                    (uint8_t)ether_addr.ether_addr_octet[3],\
                    (uint8_t)ether_addr.ether_addr_octet[4],\
                    (uint8_t)ether_addr.ether_addr_octet[5])
#define  DEBUG_PRINT(args)  debug_print args
#else
#define DEBUG_PRINT
#define DEBUG_PRINT_ETHER
#endif

static struct module_info minfo = {
    0xabbe, "brdg", 0, INFPSZ, 512, 128 
};

static struct qinit brdg_rinit = { 
    brdg_rput, NULL, brdg_open, brdg_close, NULL, &minfo, NULL 
};

static struct qinit brdg_winit = { 
    brdg_wput, NULL, NULL, NULL, NULL, &minfo, NULL 
};

struct streamtab brdg_info = {
    &brdg_rinit, &brdg_winit, NULL, NULL
};

static struct fmodsw brdg_fmodsw ={
    "brdg",
    &brdg_info,
    (D_NEW|D_MP|D_MTQPAIR|D_MTOUTPERIM|D_MTOCEXCL)
};

struct modlstrmod modlstrmod ={  
  &mod_strmodops, "bridge module(v1.15)", &brdg_fmodsw
};

static struct modlinkage modlinkage =
{
    MODREV_1, (void *)&modlstrmod, NULL                    
};

int
_init()
{
        int err;
        DEBUG_PRINT((CE_CONT,"Entering _init()\n"));        
        err = mod_install(&modlinkage);
        return err;
}

int
_info(struct modinfo *modinfop)
{
    int err;
    DEBUG_PRINT((CE_CONT,"Entering _info()\n"));    
    err = mod_info(&modlinkage, modinfop);
    return err;
}

int
_fini()
{
    int err;
    DEBUG_PRINT((CE_CONT,"Entering _finit()\n"));    
    err =  mod_remove(&modlinkage);
    return err;
}


/**********************************************************************
 * brdg_open()
 *
 * Open procedure of brdg module. 
 **********************************************************************/
static int
brdg_open(queue_t* q, dev_t *devp, int oflag, int sflag, cred_t *cred)
{
    port_t   *port;
    uint32_t portnum;

    DEBUG_PRINT((CE_CONT,"Entering brdg_open()\n"));
    if (sflag != MODOPEN) {
        return EINVAL;
    }
    
    for (portnum = 0; portnum < MAXPORT; portnum++){
        if (port_list[portnum].rqueue == NULL){
            port_list[portnum].rqueue = q;
            port = &port_list[portnum];            
            break;
        }
    }

    if (portnum >= MAXPORT)
        return(ENXIO);    
    /*
     * Set an address of port_s structure to q_ptr of read queue and write queue.
     */
    q->q_ptr = WR(q)->q_ptr = port;
    qprocson(q);
    return(0);    
}

/**********************************************************************
 * brdg_close()
 * 
 * Close procedure of brdg module.
 **********************************************************************/
static int brdg_close (queue_t *q, int flag, int sflag, cred_t *cred)
{
    port_t *port;
    node_t *node;
    uint32_t nodenum;
    
    DEBUG_PRINT((CE_CONT,"Entering brdg_close()\n"));    
    port = q->q_ptr;
    /*
     * Disable PUT and SERVICE routine.
     */
    qprocsoff(q);
    /*
     * Delete node structure
     */
    for ( nodenum =0 ; nodenum < MAXHASH ; nodenum++){
        node = &node_hash_table[nodenum];
        if ( node->port != NULL && node->port->rqueue == q){
            node->port = NULL;
        }
    }
    port->rqueue= NULL; 
    /*
     * Unlink port structure.
     */
    q->q_ptr = WR(q)->q_ptr = NULL;
    return(0);
}

/*************************************************************************
 * brdg_wput()
 *
 * Write put procedure of brdg module.
 * No one won't call this function...
 * 
 *  Arguments:
 *           q:  queue structure
 *          mp:  pointer of message block
 *  Return:
 *           None
 *************************************************************************/
static int
brdg_wput(queue_t *q, mblk_t *mp)
{
    struct iocblk *iocp;

    DEBUG_PRINT((CE_CONT,"Entering brdg_wput()\n"));
    
    putnext(q, mp);
    return(0);
}

/**********************************************************************
 * brdg_rput()
 *
 * Read procedure of brdg module.
 *
 * This function is called by putnext(9F) called by NIC driver.
 * If messages type is M_DATA, brdg_rput_data() is called.
 * 
 *  Arguments:
 *           q:  queue structure
 *          mp:  message block
 * Return:
 *           none
 ***********************************************************************/
static int
brdg_rput(queue_t *q, mblk_t *mp)
{
    struct     ether_header *ether;
    uchar_t    *rptr;       /* Read pointer */
    node_t     *snode;      /* node structure which is a list of senders */
    port_t     *port;       /* port structure */
    
    switch(mp->b_datap->db_type) {
        case M_FLUSH:
            if (*mp->b_rptr & FLUSHW) {
                *mp->b_rptr &= ~FLUSHR;
                qreply(q, mp);
            }
            else
                freemsg(mp);
            return(0);
        case M_ERROR:
        case M_HANGUP:
            freemsg(mp);
            return(0);
        case M_DATA:
            rptr = mp->b_rptr; /* Read pointer of the messages */
            ether = (struct ether_header *)&rptr[0];
            snode = &node_hash_table[ETHER_HASH(ether->ether_shost)];

            if(snode->port == NULL){
                /*
                 * The node is not registered yet.
                 */
                DEBUG_PRINT((CE_CONT,"Node not registerd. Register new node\n"));
                qwriter(q, mp, brdg_register_node, PERIM_OUTER);
                return(0);
            } else if(bcmp(&ether->ether_shost, &snode->ether_addr, ETHERADDRL)){
                /*
                 * It seems hash valus is conflict. (likely happen..)
                 */
                DEBUG_PRINT((CE_CONT,"Node address is conflict!!!!\n"));
                DEBUG_PRINT_ETHER("conflict source addr = ", ether->ether_shost);
                DEBUG_PRINT_ETHER("conflict node  addr  = ", snode->ether_addr );
                qwriter(q, mp, brdg_register_node, PERIM_OUTER);
                return(0);
            }  
            brdg_rput_data(q, mp);
            return(0);
        default:
            freemsg(mp);
            return(0);
    } /* switch() END */
}

/**********************************************************************
 * brdg_rput_data()
 *
 * Read procedure of brdg module for M_DATA message type.
 * This is called by brdg_rput() or brdg_register_node().
 * This function putnext(9F) a messages to the other network interface's queue.
 * 
 *  Arguments:
 *           q:  queue structure
 *          mp:  message block 
 *  Return:
 *           int
 ***********************************************************************/
static int
brdg_rput_data(queue_t *q, mblk_t *mp)
{
    uint32_t   portnum;
    struct     ether_header *ether;
    uchar_t    *rptr;         /* read pointer */
    node_t     *snode;        /* node structure */
    node_t     *dnode;        /* node structure */
    port_t     *port;         /* port structure */
    mblk_t     *dp;           /* duplicate message block */
    
    port = q->q_ptr;          
    rptr = mp->b_rptr;       
    ether = (struct ether_header *)&rptr[0];
    snode = &node_hash_table[ETHER_HASH(ether->ether_shost)];

    if(snode->port == NULL){
        DEBUG_PRINT((CE_CONT,"Node not registered yet. Something wrong!!!!!\n"));
        freemsg(mp);
        return(0);
    } 

    if( snode->port->rqueue == q){
        dnode = &node_hash_table[ETHER_HASH(ether->ether_dhost)];

        if( dnode->port != NULL ){

            if (dnode->port->rqueue == q){

                DEBUG_PRINT((CE_CONT,"Dest addr is registerd. But not need to forward.\n"));
                freemsg(mp);
                return(0);
            }
            if ( dnode->port->rqueue == NULL) {
                DEBUG_PRINT((CE_CONT,"Dest addr is registered. But queue does not exist\n"));
            } else {
                if ( canputnext(WR(dnode->port->rqueue))){
                    DEBUG_PRINT((CE_CONT,"Dest addr is registered. Put the msg to appropriate queue\n"));

                    putnext(WR(dnode->port->rqueue), mp);
                    return(0);
                } else {
                    DEBUG_PRINT((CE_CONT,"Dest addr is registerd. But can not put message.\n"));
                    freemsg(mp);
                    return(0);
                }
            }
        } else {
            DEBUG_PRINT_ETHER("dnode not found for this address: Ether = ", ether->ether_dhost);
            DEBUG_PRINT((CE_CONT, "dnodes's ETHER_HASH = %d\n", ETHER_HASH(ether->ether_dhost)));
            /*
             * Destination ethernet address is not registered yet.
             * Round ports and put message to all ports.
             */
            for ( portnum = 0 ; portnum < MAXPORT ; portnum++){
                if((port_list[portnum].rqueue != NULL) && (port_list[portnum].rqueue != q)){
                    if (canputnext(WR(port_list[portnum].rqueue))){
                        dp = dupmsg(mp);
                        DEBUG_PRINT((CE_CONT,"put message to port_list[%d] \n",portnum));
                        putnext(WR(port_list[portnum].rqueue), dp);
                    }
                }
            } 
            freemsg(mp);
            return(0);
        } 
    } else { /* rqueue == q ? */
        freemsg(mp);
        return(0);
    }
    return(0);
}

/*****************************************************************************
 * brdg_register_node()
 *
 * Register source ethernet address in node structure.
 * This function must be called through qwriter(9F)
 *
 *  Arguments:
 *           q:  queue structure
 *          mp:  message block 
 *  Return: 
 *           none
 *****************************************************************************/
static void 
brdg_register_node(queue_t *q, mblk_t *mp)
{
    struct ether_header  *ether;
    uchar_t              *rptr;     /* read pointer */
    node_t               *node;     /* node structure */
    port_t               *port;     /* port structure */
    
    port  = q->q_ptr;   
    rptr  = mp->b_rptr; 
    ether = (struct ether_header *)&rptr[0];
    node  = &node_hash_table[ETHER_HASH(ether->ether_shost)];

    DEBUG_PRINT((CE_CONT,"register: ETHER_HASH = %d\n",ETHER_HASH(ether->ether_shost)));
    DEBUG_PRINT_ETHER("register : Ether = ", ether->ether_shost);
    
    bcopy(ether->ether_shost.ether_addr_octet, node->ether_addr.ether_addr_octet, ETHERADDRL);
    node->port = port;

    brdg_rput_data(q, mp);
    return;
}
/*****************************************************************************
 * debug_print()
 *
 * For debug output
 *
 *  Arguments:
 *           level  :  
 *           format :  
 * Return:
 *           None
 *****************************************************************************/
void
debug_print(int level, char *format, ...)
{
    va_list     ap;
    char        buf[MAX_MSG];

    va_start(ap, format);
    vsprintf(buf, format, ap);    
    va_end(ap);
    cmn_err(level, "%s", buf);
    return;
}    
