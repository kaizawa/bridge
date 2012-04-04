/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1986, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copright (c) 2010  Kazuyoshi Aizawa <admin2@whiteboard.ne.jp>
 * All rights reserved.
 */
/******************************************************
 * 
 * dlpiutil.c
 *
 * Utilities for DLPI
 *
 *****************************************************/
#include <syslog.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stropts.h>
#include <sys/dlpi.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/signal.h>
#include <sys/stream.h>
#include <string.h>
#include <sys/varargs.h>
#include <sys/dlpi.h>
#include <errno.h>
#include <unistd.h>
#include <stropts.h>
#include "dlpiutil.h"
#ifdef SOL11
#include <sys/vfs_opreg.h>
#include <stdarg.h>
#endif

int    dlattachreq(int, t_uscalar_t, caddr_t );
int    dlpromisconreq(int, t_uscalar_t, caddr_t);
int    dlbindreq(int, t_uscalar_t, t_uscalar_t, uint16_t, uint16_t, t_uscalar_t, caddr_t);
void   dlprint_err(int , char *, ...);
int    dldetachreq(int , caddr_t);
int    dlpromiscoffreq(int, t_uscalar_t, caddr_t);
int    strioctl(int , int , int , int , char *);

#ifndef ERR_MSG_MAX
#define ERR_MSG_MAX 300
#endif

/*****************************************************************************
 * dlattachreq()
 *
 * DLPI のルーチン。putmsg(9F) を使って DL_ATTACH_REQ をドライバに送る
 * 
 *****************************************************************************/
int
dlattachreq(int fd, t_uscalar_t ppa ,caddr_t buf)
{
    union DL_primitives	 *primitive;    
    dl_attach_req_t       attachreq;
    struct strbuf         ctlbuf;
    int	                  flags = 0;
    int                   ret;
    
    attachreq.dl_primitive = DL_ATTACH_REQ;
    attachreq.dl_ppa = ppa;

    ctlbuf.maxlen = 0;
    ctlbuf.len    = sizeof(attachreq);
    ctlbuf.buf    = (caddr_t)&attachreq;

    if (putmsg(fd, &ctlbuf, (struct strbuf*) NULL, flags) < 0){
        dlprint_err(LOG_ERR, "dlattachreq: putmsg: %s", strerror(errno));
        return(-1);
    }

    ctlbuf.maxlen = MAXDLBUFSIZE;
    ctlbuf.len = 0;
    ctlbuf.buf = (caddr_t)buf;

    if ((ret = getmsg(fd, &ctlbuf, (struct strbuf *)NULL, &flags)) < 0) {
        dlprint_err(LOG_ERR, "dlattachreq: getmsg: %s\n", strerror(errno));
        return(-1);
    }

    primitive = (union DL_primitives *) ctlbuf.buf;
    if ( primitive->dl_primitive != DL_OK_ACK){
        dlprint_err(LOG_ERR, "dlattachreq: not DL_OK_ACK\n");
        return(-1);
    }
    
    return(0);
}

/*****************************************************************************
 * dlpromisconreq()
 *
 * DLPI のルーチン。 putmsg(9F) を使って DL_PROMISCON_REQ をドライバに送る
 * 
 *****************************************************************************/
int
dlpromisconreq(int fd, t_uscalar_t level, caddr_t buf)
{
    union DL_primitives	 *primitive;        
    dl_promiscon_req_t    promisconreq;
    struct strbuf         ctlbuf;
    int	                  flags = 0;
    int                   ret;

    promisconreq.dl_primitive = DL_PROMISCON_REQ;
    promisconreq.dl_level = level;

    ctlbuf.maxlen = 0;
    ctlbuf.len    = sizeof (promisconreq);
    ctlbuf.buf    = (caddr_t)&promisconreq;

    if (putmsg(fd, &ctlbuf, (struct strbuf*) NULL, flags) < 0){
        dlprint_err(LOG_ERR, "dlpromisconreq: putmsg: %s", strerror(errno));
        return(-1);
    }

    ctlbuf.maxlen = MAXDLBUFSIZE;
    ctlbuf.len = 0;
    ctlbuf.buf = (caddr_t)buf;

    if ((ret = getmsg(fd, &ctlbuf, (struct strbuf *)NULL, &flags)) < 0) {
        dlprint_err(LOG_ERR, "dlpromisconreq: getmsg: %s\n", strerror(errno));
        return(-1);
    }

    primitive = (union DL_primitives *) ctlbuf.buf;
    if ( primitive->dl_primitive != DL_OK_ACK){
        dlprint_err(LOG_ERR, "dlpromisconreq: not DL_OK_ACK\n");
        return(-1);
    }
    
    return(0); 
}

/*****************************************************************************
 * dlbindreq()
 *
 * DLPI のルーチン。 putmsg(9F) を使って DL_BIND_REQ をドライバに送る
 * 
 *****************************************************************************/
int
dlbindreq(
    int fd,
    t_uscalar_t sap,
    t_uscalar_t max_conind,
    uint16_t    service_mode,
    uint16_t    conn_mgmt,
    t_uscalar_t xidtest_flg,
    caddr_t     buf
    )
{
    union DL_primitives	 *primitive;        
    dl_bind_req_t         bindreq;
    struct strbuf	  ctlbuf;
    int	                  flags = 0;
    int                   ret;    

    bindreq.dl_primitive    = DL_BIND_REQ;
    bindreq.dl_sap          = sap;
    bindreq.dl_max_conind   = max_conind;
    bindreq.dl_service_mode = service_mode;
    bindreq.dl_conn_mgmt    = conn_mgmt;
    bindreq.dl_xidtest_flg  = xidtest_flg;

    ctlbuf.maxlen = 0;
    ctlbuf.len    = sizeof(bindreq);
    ctlbuf.buf    = (caddr_t)&bindreq;

    if (putmsg(fd, &ctlbuf, (struct strbuf*) NULL, flags) < 0){
        dlprint_err(LOG_ERR, "dlbindreq: putmsg: %s", strerror(errno));
        return(-1);
    }

    ctlbuf.maxlen = MAXDLBUFSIZE;
    ctlbuf.len    = 0;
    ctlbuf.buf    = (caddr_t)buf;

    if ((ret = getmsg(fd, &ctlbuf, (struct strbuf *)NULL, &flags)) < 0) {
        dlprint_err(LOG_ERR, "dlbindreq: getmsg: %s\n", strerror(errno));
        return(-1);
    }

    primitive = (union DL_primitives *) ctlbuf.buf;
    if ( primitive->dl_primitive != DL_BIND_ACK){
        dlprint_err(LOG_ERR, "dlbindreq: not DL_BIND_ACK\n");
        return(-1);
    }
    
    return(0);
}

/***********************************************************
 * dlprint_err()
 *
 * エラーメッセージを表示するルーチン。
 * 
 ***********************************************************/
void
dlprint_err(int level, char *format, ...)
{
    va_list ap;
    char buf[ERR_MSG_MAX];

    va_start(ap, format);
    vsnprintf(buf, ERR_MSG_MAX, format, ap);
    va_end(ap);

    if(isatty(2))
        fprintf(stderr, buf);
}

/*****************************************************************************
 * dldetachreq()
 *
 * DLPI のルーチン。putmsg(9F) を使って DL_DETACH_REQ をドライバに送る
 * 
 *****************************************************************************/
int
dldetachreq(int fd, caddr_t buf)
{
    union DL_primitives	 *primitive;    
    dl_detach_req_t       detachreq = {0}; 
    struct strbuf         ctlbuf;
    int	                  flags = 0;
    int                   ret;
    
    detachreq.dl_primitive = DL_DETACH_REQ;

    ctlbuf.maxlen = 0;
    ctlbuf.len    = sizeof(detachreq);
    ctlbuf.buf    = (caddr_t)&detachreq;

    if (putmsg(fd, &ctlbuf, (struct strbuf*) NULL, flags) < 0){
        dlprint_err(LOG_ERR, "dldetachreq: putmsg: %s", strerror(errno));
        return(-1);
    }

    ctlbuf.maxlen = MAXDLBUFSIZE;
    ctlbuf.len = 0;
    ctlbuf.buf = (caddr_t)buf;

    if ((ret = getmsg(fd, &ctlbuf, (struct strbuf *)NULL, &flags)) < 0) {
        dlprint_err(LOG_ERR, "dldetacheq: getmsg: %s\n", strerror(errno));
        return(-1);
    }

    primitive = (union DL_primitives *) ctlbuf.buf;
    if ( primitive->dl_primitive != DL_OK_ACK){
        dlprint_err(LOG_ERR, "dldetachreq: not DL_OK_ACK\n");
        return(-1);
    }
    
    return(0);
}

/*****************************************************************************
 * dlpromiscoffreq()
 *
 * DLPI のルーチン。 putmsg(9F) を使って DL_PROMISCOFF_REQ をドライバに送る
 * 
 *****************************************************************************/
int
dlpromiscoffreq(int fd, t_uscalar_t level, caddr_t buf)
{
    union DL_primitives	 *primitive;        
    dl_promiscoff_req_t    promiscoffreq;
    struct strbuf         ctlbuf;
    int	                  flags = 0;
    int                   ret;

    promiscoffreq.dl_primitive = DL_PROMISCOFF_REQ;
    promiscoffreq.dl_level = level;

    ctlbuf.maxlen = 0;
    ctlbuf.len    = sizeof (promiscoffreq);
    ctlbuf.buf    = (caddr_t)&promiscoffreq;

    if (putmsg(fd, &ctlbuf, (struct strbuf*) NULL, flags) < 0){
        dlprint_err(LOG_ERR, "dlpromiscoffreq: putmsg: %s", strerror(errno));
        return(-1);
    }

    ctlbuf.maxlen = MAXDLBUFSIZE;
    ctlbuf.len = 0;
    ctlbuf.buf = (caddr_t)buf;

    if ((ret = getmsg(fd, &ctlbuf, (struct strbuf *)NULL, &flags)) < 0) {
        dlprint_err(LOG_ERR, "dlpromiscoffreq: getmsg: %s\n", strerror(errno));
        return(-1);
    }

    primitive = (union DL_primitives *) ctlbuf.buf;
    if ( primitive->dl_primitive != DL_OK_ACK){
        dlprint_err(LOG_ERR, "dlpromiscoffreq: not DL_OK_ACK\n");
        return(-1);
    }
    
    return(0); 
}

/*****************************************************************************
 * strioctl()
 *
 * STREAMS デバイスのための ioctl ルーチン。
 * オープンした STREAMS デバイスに対して ioctl コマンドを送信する
 * 
 *****************************************************************************/
int
strioctl(int fd, int cmd, int timout, int len, char *dp)
{
    struct strioctl  strioc;
    int	             ret;

    strioc.ic_cmd = cmd;
    strioc.ic_timout = timout;
    strioc.ic_len = len;
    strioc.ic_dp = dp;
    
    ret = ioctl(fd, I_STR, &strioc);

    if (ret < 0){
        dlprint_err(LOG_ERR, "strioctl: %s (cmd = 0x%x)\n", strerror(errno), cmd);                    
        return (-1);
    } else {
        return (strioc.ic_len);
    }
}
