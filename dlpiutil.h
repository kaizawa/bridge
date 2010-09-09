/****************************************************************
 * dlpiutil.h
 *
 * Copright (c) 2010  Kazuyoshi Aizawa <admin2@whiteboard.ne.jp> 
 * 
 ***************************************************************/

#ifndef __DLPIUTIL_H
#define __DLPIUTIL_H

#define MAXDLBUFSIZE  8192
#define DLBUFSIZE     8192

extern int    dlattachreq(int, t_uscalar_t, caddr_t );
extern int    dlpromisconreq(int, t_uscalar_t, caddr_t);
extern int    dlbindreq(int, t_uscalar_t, t_uscalar_t, uint16_t, uint16_t, t_uscalar_t, caddr_t);
extern void   dlprint_err(int , char *, ...);
extern int    dldetachreq(int , caddr_t);
extern int    dlpromiscoffreq(int, t_uscalar_t, caddr_t);
extern int    strioctl(int , int , int , int , char *);

#endif /* __DLPIUTIL_H */
