// Filename: qpcamera.h
// Created by:  drose (26Feb02)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) 2001, Disney Enterprises, Inc.  All rights reserved
//
// All use of this software is subject to the terms of the Panda 3d
// Software license.  You should have received a copy of this license
// along with this source code; you will also find a current copy of
// the license at http://www.panda3d.org/license.txt .
//
// To contact the maintainers of this program write to
// panda3d@yahoogroups.com .
//
////////////////////////////////////////////////////////////////////

#ifndef qpCAMERA_H
#define qpCAMERA_H

#include "pandabase.h"

#include "qplensNode.h"
#include "qpnodePath.h"

class DisplayRegion;

////////////////////////////////////////////////////////////////////
//       Class : qpCamera
// Description : A node that can be positioned around in the scene
//               graph to represent a point of view for rendering a
//               scene.
////////////////////////////////////////////////////////////////////
class EXPCL_PANDA qpCamera : public qpLensNode {
PUBLISHED:
  qpCamera(const string &name);

protected:
  qpCamera(const qpCamera &copy);
public:
  virtual ~qpCamera();

  virtual PandaNode *make_copy() const;
  virtual bool safe_to_flatten() const;
  virtual bool safe_to_transform() const;

PUBLISHED:
  INLINE void set_active(bool active);
  INLINE bool is_active() const;

  INLINE void set_scene(const qpNodePath &scene);
  INLINE const qpNodePath &get_scene() const;

  INLINE int get_num_display_regions() const;
  INLINE DisplayRegion *get_display_region(int n) const;

private:
  void add_display_region(DisplayRegion *display_region);
  void remove_display_region(DisplayRegion *display_region);

  bool _active;
  qpNodePath _scene;

  typedef pvector<DisplayRegion *> DisplayRegions;
  DisplayRegions _display_regions;

public:
  static void register_with_read_factory();
  virtual void write_datagram(BamWriter *manager, Datagram &dg);

protected:
  static TypedWritable *make_from_bam(const FactoryParams &params);
  void fillin(DatagramIterator &scan, BamReader *manager);

public:
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    qpLensNode::init_type();
    register_type(_type_handle, "qpCamera",
                  qpLensNode::get_class_type());
  }
  virtual TypeHandle get_type() const {
    return get_class_type();
  }
  virtual TypeHandle force_init_type() {init_type(); return get_class_type();}

private:
  static TypeHandle _type_handle;

  friend class DisplayRegion;
};

#include "qpcamera.I"

#endif
