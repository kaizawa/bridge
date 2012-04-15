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
/****************************************************************
 * dlpiutil.h
 ***************************************************************/

#ifndef __DLPIUTIL_H
#define __DLPIUTIL_H

#define MAXDLBUFSIZE  8192
#define DLBUFSIZE     8192

extern int    dlattachreq(int, ulong, caddr_t );
extern int    dlpromisconreq(int, ulong, caddr_t);
extern int    dlbindreq(int, ulong, ulong, uint16_t, uint16_t, ulong, caddr_t);
extern void   dlprint_err(int , char *, ...);
extern int    dldetachreq(int , caddr_t);
extern int    dlpromiscoffreq(int, ulong, caddr_t);
extern int    strioctl(int , int , int , int , char *);

#endif /* __DLPIUTIL_H */
