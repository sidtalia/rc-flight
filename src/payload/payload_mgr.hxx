/**
 * \file: payload_mgr.h
 *
 * Top level payload manager
 *
 * Copyright (C) 2013 - Curtis L. Olson curtolson@flightgear.org
 */


#ifndef _AURA_PAYLOAD_MGR_H
#define _AURA_PAYLOAD_MGR_H


#include "python/pyprops.hxx"


class UGPayloadMgr {

public:

    UGPayloadMgr();
    ~UGPayloadMgr();

    void bind();
    void init();
    bool update();
    void close();

};


// global reference to payload mgr (move to globals?)
extern UGPayloadMgr payload_mgr;


#endif // _AURA_PAYLOAD_MGR_H
