// Filename: pandaNode.cxx
// Created by:  drose (20Feb02)
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

#include "pandaNode.h"
#include "config_pgraph.h"
#include "qpnodePathComponent.h"
#include "bamReader.h"
#include "bamWriter.h"
#include "indent.h"
#include "geometricBoundingVolume.h"


TypeHandle PandaNode::_type_handle;


////////////////////////////////////////////////////////////////////
//     Function: PandaNode::CData::Copy Constructor
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
PandaNode::CData::
CData(const PandaNode::CData &copy) :
  _down(copy._down),
  _up(copy._up),
  _chains(copy._chains),
  _state(copy._state),
  _transform(copy._transform)
{
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::CData::make_copy
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
CycleData *PandaNode::CData::
make_copy() const {
  return new CData(*this);
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::CData::write_datagram
//       Access: Public, Virtual
//  Description: Writes the contents of this object to the datagram
//               for shipping out to a Bam file.
////////////////////////////////////////////////////////////////////
void PandaNode::CData::
write_datagram(BamWriter *manager, Datagram &dg) const {
  manager->write_pointer(dg, _state);
  manager->write_pointer(dg, _transform);

  // When we write a PandaNode, we write out its complete list of
  // child node pointers, but we only write out the parent node
  // pointers that have already been added to the bam file by a
  // previous write operation.  This is a bit of trickery that allows
  // us to write out just a subgraph (instead of the complete graph)
  // when we write out an arbitrary node in the graph, yet also allows
  // us to keep nodes completely in sync when we use the bam format
  // for streaming scene graph operations over the network.

  int num_parents = 0;
  Up::const_iterator ui;
  for (ui = _up.begin(); ui != _up.end(); ++ui) {
    PandaNode *parent_node = (*ui).get_parent();
    if (manager->has_object(parent_node)) {
      num_parents++;
    }
  }
  nassertv(num_parents == (int)(PN_uint16)num_parents);
  dg.add_uint16(num_parents);
  for (ui = _up.begin(); ui != _up.end(); ++ui) {
    PandaNode *parent_node = (*ui).get_parent();
    if (manager->has_object(parent_node)) {
      manager->write_pointer(dg, parent_node);
    }
  }

  int num_children = _down.size();
  nassertv(num_children == (int)(PN_uint16)num_children);
  dg.add_uint16(num_children);

  // **** We should smarten up the writing of the sort number--most of
  // the time these will all be zero.
  Down::const_iterator ci;
  for (ci = _down.begin(); ci != _down.end(); ++ci) {
    PandaNode *child_node = (*ci).get_child();
    int sort = (*ci).get_sort();
    manager->write_pointer(dg, child_node);
    dg.add_int32(sort);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::CData::complete_pointers
//       Access: Public, Virtual
//  Description: Receives an array of pointers, one for each time
//               manager->read_pointer() was called in fillin().
//               Returns the number of pointers processed.
////////////////////////////////////////////////////////////////////
int PandaNode::CData::
complete_pointers(TypedWritable **p_list, BamReader *manager) {
  int pi = CycleData::complete_pointers(p_list, manager);

  // Get the state and transform pointers.
  _state = DCAST(RenderState, p_list[pi++]);
  _transform = DCAST(TransformState, p_list[pi++]);

  // Get the parent pointers.
  Up::iterator ui;
  for (ui = _up.begin(); ui != _up.end(); ++ui) {
    PT(PandaNode) parent_node = DCAST(PandaNode, p_list[pi++]);
    (*ui) = UpConnection(parent_node);
  }

  // Get the child pointers.
  Down::iterator di;
  for (di = _down.begin(); di != _down.end(); ++di) {
    int sort = (*di).get_sort();
    PT(PandaNode) child_node = DCAST(PandaNode, p_list[pi++]);
    (*di) = DownConnection(child_node, sort);
  }

  return pi;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::CData::fillin
//       Access: Public, Virtual
//  Description: This internal function is called by make_from_bam to
//               read in all of the relevant data from the BamFile for
//               the new PandaNode.
////////////////////////////////////////////////////////////////////
void PandaNode::CData::
fillin(DatagramIterator &scan, BamReader *manager) {
  // Read the state and transform pointers.
  manager->read_pointer(scan);
  manager->read_pointer(scan);

  int num_parents = scan.get_uint16();
  // Read the list of parent nodes.  Push back a NULL for each one.
  _up.reserve(num_parents);
  for (int i = 0; i < num_parents; i++) {
    manager->read_pointer(scan);
    _up.push_back(UpConnection(NULL));
  }

  int num_children = scan.get_uint16();
  // Read the list of child nodes.  Push back a NULL for each one.
  _down.reserve(num_children);
  for (int i = 0; i < num_children; i++) {
    manager->read_pointer(scan);
    int sort = scan.get_int32();
    _down.push_back(DownConnection(NULL, sort));
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::Constructor
//       Access: Published
//  Description:
////////////////////////////////////////////////////////////////////
PandaNode::
PandaNode(const string &name) :
  Namable(name)
{
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::Destructor
//       Access: Published, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
PandaNode::
~PandaNode() {
  // We shouldn't have any parents left by the time we destruct, or
  // there's a refcount fault somewhere.
#ifndef NDEBUG
  {
    CDReader cdata(_cycler);
    nassertv(cdata->_up.empty());
  }
#endif  // NDEBUG

  remove_all_children();
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::Copy Constructor
//       Access: Protected
//  Description: Do not call the copy constructor directly; instead,
//               use make_copy() or copy_subgraph() to make a copy of
//               a node.
////////////////////////////////////////////////////////////////////
PandaNode::
PandaNode(const PandaNode &copy) :
  TypedWritable(copy),
  Namable(copy),
  ReferenceCount(copy)
{
  // Copying a node does not copy its children.

  // Copy the other node's state.
  CDReader copy_cdata(copy._cycler);
  CDWriter cdata(_cycler);
  cdata->_state = copy_cdata->_state;
  cdata->_transform = copy_cdata->_transform;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::Copy Assignment Operator
//       Access: Private
//  Description: Do not call the copy assignment operator at all.  Use
//               make_copy() or copy_subgraph() to make a copy of a
//               node.
////////////////////////////////////////////////////////////////////
void PandaNode::
operator = (const PandaNode &copy) {
  nassertv(false);
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::make_copy
//       Access: Public, Virtual
//  Description: Returns a newly-allocated PandaNode that is a shallow
//               copy of this one.  It will be a different pointer,
//               but its internal data may or may not be shared with
//               that of the original PandaNode.  No children will be
//               copied.
////////////////////////////////////////////////////////////////////
PandaNode *PandaNode::
make_copy() const {
  return new PandaNode(*this);
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::copy_subgraph
//       Access: Public
//  Description: Allocates and returns a complete copy of this
//               PandaNode and the entire scene graph rooted at this
//               PandaNode.  Some data may still be shared from the
//               original (e.g. vertex index tables), but nothing that
//               will impede normal use of the PandaNode.
////////////////////////////////////////////////////////////////////
PandaNode *PandaNode::
copy_subgraph() const {
  // *** Do something here.
  nassertr(false, (PandaNode *)NULL);
  return (PandaNode *)NULL;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::safe_to_flatten
//       Access: Public, Virtual
//  Description: Returns true if it is generally safe to flatten out
//               this particular kind of PandaNode by duplicating
//               instances, false otherwise (for instance, a Camera
//               cannot be safely flattened, because the Camera
//               pointer itself is meaningful).
////////////////////////////////////////////////////////////////////
bool PandaNode::
safe_to_flatten() const {
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::safe_to_transform
//       Access: Public, Virtual
//  Description: Returns true if it is generally safe to transform
//               this particular kind of PandaNode by calling the
//               xform() method, false otherwise.  For instance, it's
//               usually a bad idea to attempt to xform a Character.
////////////////////////////////////////////////////////////////////
bool PandaNode::
safe_to_transform() const {
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::safe_to_combine
//       Access: Public, Virtual
//  Description: Returns true if it is generally safe to combine this
//               particular kind of PandaNode with other kinds of
//               PandaNodes, adding children or whatever.  For
//               instance, an LODNode should not be combined with any
//               other PandaNode, because its set of children is
//               meaningful.
////////////////////////////////////////////////////////////////////
bool PandaNode::
safe_to_combine() const {
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::xform
//       Access: Public, Virtual
//  Description: Transforms the contents of this PandaNode by the
//               indicated matrix, if it means anything to do so.  For
//               most kinds of PandaNodes, this does nothing.
////////////////////////////////////////////////////////////////////
void PandaNode::
xform(const LMatrix4f &) {
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::combine_with
//       Access: Public, Virtual
//  Description: Collapses this PandaNode with the other PandaNode, if
//               possible, and returns a pointer to the combined
//               PandaNode, or NULL if the two PandaNodes cannot
//               safely be combined.
//
//               The return value may be this, other, or a new
//               PandaNode altogether.
//
//               This function is called from GraphReducer::flatten(),
//               and need not deal with children; its job is just to
//               decide whether to collapse the two PandaNodes and
//               what the collapsed PandaNode should look like.
////////////////////////////////////////////////////////////////////
PandaNode *PandaNode::
combine_with(PandaNode *other) {
  // An unadorned PandaNode always combines with any other PandaNodes by
  // yielding completely.  However, if we are actually some fancy PandaNode
  // type that derives from PandaNode but didn't redefine this function, we
  // should refuse to combine.
  if (is_exact_type(get_class_type())) {
    // No, we're an ordinary PandaNode.
    return other;

  } else if (other->is_exact_type(get_class_type())) {
    // We're not an ordinary PandaNode, but the other one is.
    return this;
  }

  // We're something other than an ordinary PandaNode.  Don't combine.
  return (PandaNode *)NULL;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::has_cull_callback
//       Access: Public, Virtual
//  Description: Should be overridden by derived classes to return
//               true if cull_callback() has been defined.  Otherwise,
//               returns false to indicate cull_callback() does not
//               need to be called for this node during the cull
//               traversal.
////////////////////////////////////////////////////////////////////
bool PandaNode::
has_cull_callback() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::cull_callback
//       Access: Public, Virtual
//  Description: If has_cull_callback() returns true, this function
//               will be called during the cull traversal to perform
//               any additional operations that should be performed at
//               cull time.  This may include additional manipulation
//               of render state or additional visible/invisible
//               decisions, or any other arbitrary operation.
//
//               By the time this function is called, the node has
//               already passed the bounding-volume test for the
//               viewing frustum, and the node's transform and state
//               have already been applied to the indicated
//               CullTraverserData object.
//
//               The return value is true if this node should be
//               visible, or false if it should be culled.
////////////////////////////////////////////////////////////////////
bool PandaNode::
cull_callback(CullTraverserData &) {
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::has_selective_visibility
//       Access: Public, Virtual
//  Description: Should be overridden by derived classes to return
//               true if this kind of node has some restrictions on
//               the set of children that should be rendered.  Node
//               with this property include LODNodes, SwitchNodes, and
//               SequenceNodes.
//
//               If this function returns true,
//               get_first_visible_child() and
//               get_next_visible_child() will be called to walk
//               through the list of children during cull, instead of
//               iterating through the entire list.  This method is
//               called after cull_callback(), so cull_callback() may
//               be responsible for the decisions as to which children
//               are visible at the moment.
////////////////////////////////////////////////////////////////////
bool PandaNode::
has_selective_visibility() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::get_first_visible_child
//       Access: Public, Virtual
//  Description: Returns the index number of the first visible child
//               of this node, or a number >= get_num_children() if
//               there are no visible children of this node.  This is
//               called during the cull traversal, but only if
//               has_selective_visibility() has already returned true.
//               See has_selective_visibility().
////////////////////////////////////////////////////////////////////
int PandaNode::
get_first_visible_child() const {
  return 0;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::get_next_visible_child
//       Access: Public, Virtual
//  Description: Returns the index number of the next visible child
//               of this node following the indicated child, or a
//               number >= get_num_children() if there are no more
//               visible children of this node.  See
//               has_selective_visibility() and
//               get_first_visible_child().
////////////////////////////////////////////////////////////////////
int PandaNode::
get_next_visible_child(int n) const {
  return n + 1;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::find_child
//       Access: Published
//  Description: Returns the index of the indicated child node, if it
//               is a child, or -1 if it is not.
////////////////////////////////////////////////////////////////////
int PandaNode::
find_child(PandaNode *node) const {
  CDReader cdata(_cycler);

  // We have to search for the child by brute force, since we don't
  // know what sort index it was added as.
  Down::const_iterator ci;
  for (ci = cdata->_down.begin(); ci != cdata->_down.end(); ++ci) {
    if ((*ci).get_child() == node) {
      return ci - cdata->_down.begin();
    }
  }

  return -1;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::add_child
//       Access: Published
//  Description: Adds a new child to the node.  The child is added in
//               the relative position indicated by sort; if all
//               children have the same sort index, the child is added
//               at the end.
//
//               If the same child is added to a node more than once,
//               the previous instance is first removed.
////////////////////////////////////////////////////////////////////
void PandaNode::
add_child(PandaNode *child_node, int sort) {
  // Ensure the child_node is not deleted while we do this.
  {
    PT(PandaNode) keep_child = child_node;
    remove_child(child_node);
    CDWriter cdata(_cycler);
    CDWriter cdata_child(child_node->_cycler);
    
    cdata->_down.insert(DownConnection(child_node, sort));
    cdata_child->_up.insert(UpConnection(this));
    
    // We also have to adjust any qpNodePathComponents the child might
    // have that reference the child as a top node.  Any other
    // components we can leave alone, because we are making a new
    // instance of the child.
    Chains::iterator ci;
    for (ci = cdata_child->_chains.begin();
	 ci != cdata_child->_chains.end();
	 ++ci) {
      if ((*ci)->is_top_node()) {
	(*ci)->set_next(get_generic_component());
      }
    }
    child_node->fix_chain_lengths(cdata_child);
  }

  // Mark the bounding volumes stale.
  force_bound_stale();
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::remove_child
//       Access: Published
//  Description: Removes the nth child from the node.
////////////////////////////////////////////////////////////////////
void PandaNode::
remove_child(int n) {
  {
    CDWriter cdata(_cycler);
    nassertv(n >= 0 && n < (int)cdata->_down.size());
    
    PT(PandaNode) child_node = cdata->_down[n].get_child();
    CDWriter cdata_child(child_node->_cycler);
    
    cdata->_down.erase(cdata->_down.begin() + n);
    int num_erased = cdata_child->_up.erase(UpConnection(this));
    nassertv(num_erased == 1);
    
    // Now sever any qpNodePathComponents on the child that reference
    // this node.  If we have multiple of these, we have to collapse
    // them together.
    qpNodePathComponent *collapsed = (qpNodePathComponent *)NULL;
    Chains::iterator ci;
    ci = cdata_child->_chains.begin();
    while (ci != cdata_child->_chains.end()) {
      Chains::iterator cnext = ci;
      ++cnext;
      if (!(*ci)->is_top_node() && (*ci)->get_next()->get_node() == this) {
	if (collapsed == (qpNodePathComponent *)NULL) {
	  (*ci)->set_top_node();
	  collapsed = (*ci);
	} else {
	  // This is a different component that used to reference a
	  // different instance, but now it's all just the same topnode.
	  // We have to collapse this and the previous one together.
	  // However, there might be some qpNodePaths out there that
	  // still keep a pointer to this one, so we can't remove it
	  // altogether.
	  (*ci)->collapse_with(collapsed);
	  cdata_child->_chains.erase(ci);
	}
      }
      ci = cnext;
    }
    
    child_node->fix_chain_lengths(cdata_child);
  }

  // Mark the bounding volumes stale.
  force_bound_stale();
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::remove_child
//       Access: Published
//  Description: Removes the indicated child from the node.  Returns
//               true if the child was removed, false if it was not
//               already a child of the node.
////////////////////////////////////////////////////////////////////
bool PandaNode::
remove_child(PandaNode *child_node) {
  // Ensure the child_node is not deleted while we do this.
  {
    PT(PandaNode) keep_child = child_node;
    
    CDWriter cdata(_cycler);
    CDWriter cdata_child(child_node->_cycler);
    
    // First, look for and remove this node from the child's parent
    // list.
    int num_erased = cdata_child->_up.erase(UpConnection(this));
    if (num_erased == 0) {
      // No such node; it wasn't our child to begin with.
      return false;
    }
    
    // Now sever any qpNodePathComponents on the child that reference
    // this node.  If we have multiple of these, we have to collapse
    // them together (see above).
    qpNodePathComponent *collapsed = (qpNodePathComponent *)NULL;
    Chains::iterator ci;
    ci = cdata_child->_chains.begin();
    while (ci != cdata_child->_chains.end()) {
      Chains::iterator cnext = ci;
      ++cnext;
      if (!(*ci)->is_top_node() && (*ci)->get_next()->get_node() == this) {
	if (collapsed == (qpNodePathComponent *)NULL) {
	  (*ci)->set_top_node();
	  collapsed = (*ci);
	} else {
	  (*ci)->collapse_with(collapsed);
	  cdata_child->_chains.erase(ci);
	}
      }
      ci = cnext;
    }
    
    child_node->fix_chain_lengths(cdata_child);
    
    // Now, look for and remove the child node from our down list.
    Down::iterator di;
    bool found = false;
    for (di = cdata->_down.begin(); di != cdata->_down.end() && !found; ++di) {
      if ((*di).get_child() == child_node) {
	cdata->_down.erase(di);
	found = true;
      }
    }

    nassertr(found, false);
  }

  // Mark the bounding volumes stale.
  force_bound_stale();
  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::remove_all_children
//       Access: Published
//  Description: Removes all the children from the node at once.
////////////////////////////////////////////////////////////////////
void PandaNode::
remove_all_children() {
  {
    CDWriter cdata(_cycler);
    Down::iterator ci;
    for (ci = cdata->_down.begin(); ci != cdata->_down.end(); ++ci) {
      PT(PandaNode) child_node = (*ci).get_child();
      CDWriter cdata_child(child_node->_cycler);
      cdata_child->_up.erase(UpConnection(this));
      
      // Now sever any qpNodePathComponents on the child that reference
      // this node.  If we have multiple of these, we have to collapse
      // them together (see above).
      qpNodePathComponent *collapsed = (qpNodePathComponent *)NULL;
      Chains::iterator ci;
      ci = cdata_child->_chains.begin();
      while (ci != cdata_child->_chains.end()) {
        Chains::iterator cnext = ci;
        ++cnext;
        if (!(*ci)->is_top_node() && (*ci)->get_next()->get_node() == this) {
          if (collapsed == (qpNodePathComponent *)NULL) {
            (*ci)->set_top_node();
            collapsed = (*ci);
          } else {
            (*ci)->collapse_with(collapsed);
            cdata_child->_chains.erase(ci);
          }
        }
        ci = cnext;
      }
      
      child_node->fix_chain_lengths(cdata_child);
    }
  }

  // Mark the bounding volumes stale.
  force_bound_stale();
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::output
//       Access: Published, Virtual
//  Description: 
////////////////////////////////////////////////////////////////////
void PandaNode::
output(ostream &out) const {
  out << get_type() << " " << get_name();
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::write
//       Access: Published, Virtual
//  Description: 
////////////////////////////////////////////////////////////////////
void PandaNode::
write(ostream &out, int indent_level) const {
  indent(out, indent_level) << *this;
  CDReader cdata(_cycler);
  if (!cdata->_transform->is_identity()) {
    out << " " << *cdata->_transform;
  }
  if (!cdata->_state->is_empty()) {
    out << " " << *cdata->_state;
  }
  out << "\n";
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::is_geom_node
//       Access: Public, Virtual
//  Description: A simple downcast check.  Returns true if this kind
//               of node happens to inherit from GeomNode, false
//               otherwise.
//
//               This is provided as a a faster alternative to calling
//               is_of_type(GeomNode::get_class_type()), since this
//               test is so important to rendering.
////////////////////////////////////////////////////////////////////
bool PandaNode::
is_geom_node() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::propagate_stale_bound
//       Access: Protected, Virtual
//  Description: Called by BoundedObject::mark_bound_stale(), this
//               should make sure that all bounding volumes that
//               depend on this one are marked stale also.
////////////////////////////////////////////////////////////////////
void PandaNode::
propagate_stale_bound() {
  // Mark all of our parent nodes stale as well.
  CDWriter cdata(_cycler);
  Up::const_iterator ui;
  for (ui = cdata->_up.begin(); ui != cdata->_up.end(); ++ui) {
    PandaNode *parent_node = (*ui).get_parent();
    parent_node->mark_bound_stale();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::recompute_bound
//       Access: Protected, Virtual
//  Description: Recomputes the dynamic bounding volume for this
//               object.  The default behavior is the compute an empty
//               bounding volume; this may be overridden to extend it
//               to create a nonempty bounding volume.  However, after
//               calling this function, it is guaranteed that the
//               _bound pointer will not be shared with any other
//               stage of the pipeline, and this new pointer is
//               returned.
////////////////////////////////////////////////////////////////////
BoundingVolume *PandaNode::
recompute_bound() {
  // First, get ourselves a fresh, empty bounding volume.
  BoundingVolume *bound = BoundedObject::recompute_bound();
  nassertr(bound != (BoundingVolume*)NULL, bound);

  // Now actually compute the bounding volume by putting it around all
  // of our child bounding volumes.

  pvector<const BoundingVolume *> child_volumes;

  // It goes around this node's internal bounding volume . . .
  child_volumes.push_back(&get_internal_bound());

  CDReader cdata(_cycler);
  Down::const_iterator di;
  for (di = cdata->_down.begin(); di != cdata->_down.end(); ++di) {
    // . . . plus each node's external bounding volume.
    PandaNode *child = (*di).get_child();
    const BoundingVolume &child_bound = child->get_bound();
    child_volumes.push_back(&child_bound);
  }

  const BoundingVolume **child_begin = &child_volumes[0];
  const BoundingVolume **child_end = child_begin + child_volumes.size();

  bool success =
    bound->around(child_begin, child_end);

#ifndef NDEBUG
  if (!success) {
    pgraph_cat.error()
      << "Unable to recompute bounding volume for " << *this << ":\n"
      << "Cannot put " << bound->get_type() << " around:\n";
    for (int i = 0; i < (int)child_volumes.size(); i++) {
      pgraph_cat.error(false)
        << "  " << *child_volumes[i] << "\n";
    }
  }
#endif

  // Now, if we have a transform, apply it to the bounding volume we
  // just computed.
  const TransformState *transform = get_transform();
  if (!transform->is_identity()) {
    GeometricBoundingVolume *gbv;
    DCAST_INTO_R(gbv, bound, bound);
    gbv->xform(transform->get_mat());
  }

  return bound;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::recompute_internal_bound
//       Access: Protected, Virtual
//  Description: Called when needed to recompute the node's
//               _internal_bound object.  Nodes that contain anything
//               of substance should redefine this to do the right
//               thing.
////////////////////////////////////////////////////////////////////
BoundingVolume *PandaNode::
recompute_internal_bound() {
  return _internal_bound.recompute_bound();
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::attach
//       Access: Private, Static
//  Description: Creates a new parent-child relationship, and returns
//               the new qpNodePathComponent.  If the child was already
//               attached to the indicated parent, repositions it and
//               returns the original qpNodePathComponent.
////////////////////////////////////////////////////////////////////
PT(qpNodePathComponent) PandaNode::
attach(qpNodePathComponent *parent, PandaNode *child_node, int sort) {
  nassertr(parent != (qpNodePathComponent *)NULL, (qpNodePathComponent *)NULL);

  // See if the child was already attached to the parent.  If it was,
  // we'll use that same qpNodePathComponent.
  PT(qpNodePathComponent) child = get_component(parent, child_node);

  if (child == (qpNodePathComponent *)NULL) {
    // The child was not already attached to the parent, so get a new
    // component.
    child = get_top_component(child_node);
  }

  reparent(parent, child, sort);
  return child;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::detach
//       Access: Private, Static
//  Description: Breaks a parent-child relationship.
////////////////////////////////////////////////////////////////////
void PandaNode::
detach(qpNodePathComponent *child) {
  nassertv(child != (qpNodePathComponent *)NULL);
  nassertv(!child->is_top_node());

  PandaNode *child_node = child->get_node();
  PandaNode *parent_node = child->get_next()->get_node();
  
  {
    // Break the qpNodePathComponent connection.
    child->set_top_node();
    
    CDWriter cdata_child(child_node->_cycler);
    CDWriter cdata_parent(parent_node->_cycler);
    
    // Any other components in the same child_node that previously
    // referenced the same parent has now become invalid and must be
    // collapsed into this one and removed from the chains set.
    Chains::iterator ci;
    ci = cdata_child->_chains.begin();
    while (ci != cdata_child->_chains.end()) {
      Chains::iterator cnext = ci;
      ++cnext;
      if ((*ci) != child && !(*ci)->is_top_node() && 
	  (*ci)->get_next()->get_node() == parent_node) {
	(*ci)->collapse_with(child);
	cdata_child->_chains.erase(ci);
      }
      ci = cnext;
    }
    
    // Now look for the child and break the actual connection.
    
    // First, look for and remove the parent node from the child's up
    // list.
    int num_erased = cdata_child->_up.erase(UpConnection(parent_node));
    nassertv(num_erased == 1);
    
    child_node->fix_chain_lengths(cdata_child);
    
    // Now, look for and remove the child node from the parent's down list.
    Down::iterator di;
    bool found = false;
    for (di = cdata_parent->_down.begin(); 
	 di != cdata_parent->_down.end() && !found; 
	 ++di) {
      if ((*di).get_child() == child_node) {
	cdata_parent->_down.erase(di);
	found = true;
      }
    }
    nassertv(found);
  }

  // Mark the bounding volumes stale.
  parent_node->force_bound_stale();
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::reparent
//       Access: Private, Static
//  Description: Switches a node from one parent to another.
////////////////////////////////////////////////////////////////////
void PandaNode::
reparent(qpNodePathComponent *new_parent, qpNodePathComponent *child, int sort) {
  nassertv(new_parent != (qpNodePathComponent *)NULL);
  nassertv(child != (qpNodePathComponent *)NULL);

  if (!child->is_top_node()) {
    detach(child);
  }

  // Adjust the qpNodePathComponents.
  child->set_next(new_parent);

  PandaNode *child_node = child->get_node();
  PandaNode *parent_node = new_parent->get_node();

  {
    // Now reattach at the indicated sort position.
    CDWriter cdata_parent(parent_node->_cycler);
    CDWriter cdata_child(child_node->_cycler);
    
    cdata_parent->_down.insert(DownConnection(child_node, sort));
    cdata_child->_up.insert(UpConnection(parent_node));
    
    cdata_child->_chains.insert(child);
    child_node->fix_chain_lengths(cdata_child);
  }

  // Mark the bounding volumes stale.
  parent_node->force_bound_stale();
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::get_component
//       Access: Private, Static
//  Description: Returns the qpNodePathComponent based on the indicated
//               child of the given parent, or NULL if there is no
//               such parent-child relationship.
////////////////////////////////////////////////////////////////////
PT(qpNodePathComponent) PandaNode::
get_component(qpNodePathComponent *parent, PandaNode *child_node) {
  nassertr(parent != (qpNodePathComponent *)NULL, (qpNodePathComponent *)NULL);
  PandaNode *parent_node = parent->get_node();

  {
    CDReader cdata_child(child_node->_cycler);

    // First, walk through the list of qpNodePathComponents we already
    // have on the child, looking for one that already exists,
    // referencing the indicated parent component.
    Chains::const_iterator ci;
    for (ci = cdata_child->_chains.begin(); 
         ci != cdata_child->_chains.end(); 
         ++ci) {
      if ((*ci)->get_next() == parent) {
        // If we already have such a component, just return it.
        return (*ci);
      }
    }
  }
    
  // We don't already have a qpNodePathComponent referring to this
  // parent-child relationship.  Are they actually related?
  int child_index = child_node->find_parent(parent_node);
  if (child_index >= 0) {
    // They are.  Create and return a new one.
    PT(qpNodePathComponent) child = 
      new qpNodePathComponent(child_node, parent);
    CDWriter cdata_child(child_node->_cycler);
    cdata_child->_chains.insert(child);
    return child;
  } else {
    // They aren't related.  Return NULL.
    return NULL;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::get_top_component
//       Access: Private, Static
//  Description: Returns a qpNodePathComponent referencing the
//               indicated node as a singleton.  It is invalid to call
//               this for a node that has parents, unless you are
//               about to create a new instance (and immediately
//               reconnect the qpNodePathComponent elsewhere).
////////////////////////////////////////////////////////////////////
PT(qpNodePathComponent) PandaNode::
get_top_component(PandaNode *child_node) {
  {
    CDReader cdata_child(child_node->_cycler);

    // Walk through the list of qpNodePathComponents we already have on
    // the child, looking for one that already exists as a top node.
    Chains::const_iterator ci;
    for (ci = cdata_child->_chains.begin(); 
         ci != cdata_child->_chains.end(); 
         ++ci) {
      if ((*ci)->is_top_node()) {
        // If we already have such a component, just return it.
        return (*ci);
      }
    }
  }

  // We don't already have such a qpNodePathComponent; create and
  // return a new one.
  PT(qpNodePathComponent) child = 
    new qpNodePathComponent(child_node, (qpNodePathComponent *)NULL);
  CDWriter cdata_child(child_node->_cycler);
  cdata_child->_chains.insert(child);

  return child;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::get_generic_component
//       Access: Private
//  Description: Returns a qpNodePathComponent referencing this node as
//               a chain from the root.  It is only valid to call this
//               if there is an unambiguous path from the root;
//               otherwise, a warning will be issued and one path will
//               be chosen arbitrarily.
////////////////////////////////////////////////////////////////////
PT(qpNodePathComponent) PandaNode::
get_generic_component() {
  int num_parents = get_num_parents();
  if (num_parents == 0) {
    return get_top_component(this);

  } else {
    if (num_parents != 1) {
      pgraph_cat.warning()
        << *this << " has " << num_parents
        << " parents; choosing arbitrary path to root.\n";
    }
    PT(qpNodePathComponent) parent = get_parent(0)->get_generic_component();
    return get_component(parent, this);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::delete_component
//       Access: Private
//  Description: Removes a qpNodePathComponent from the set prior to
//               its deletion.  This should only be called by the
//               qpNodePathComponent destructor.
////////////////////////////////////////////////////////////////////
void PandaNode::
delete_component(qpNodePathComponent *component) {
  // We have to remove the component from all of the pipeline stages,
  // not just the current one.
  int max_num_erased = 0;

  int num_stages = _cycler.get_num_stages();
  for (int i = 0; i < num_stages; i++) {
    if (_cycler.is_stage_unique(i)) {
      CData *cdata = _cycler.write_stage(i);
      int num_erased = cdata->_chains.erase(component);
      max_num_erased = max(max_num_erased, num_erased);
      _cycler.release_write_stage(i, cdata);
    }
  }
  nassertv(max_num_erased == 1);
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::fix_chain_lengths
//       Access: Private
//  Description: Recursively fixes the _length member of each
//               qpNodePathComponent at this level and below, after an
//               add or delete child operation that might have messed
//               these up.
////////////////////////////////////////////////////////////////////
void PandaNode::
fix_chain_lengths(const CData *cdata) {
  bool any_wrong = false;

  Chains::const_iterator ci;
  for (ci = cdata->_chains.begin(); ci != cdata->_chains.end(); ++ci) {
    if ((*ci)->fix_length()) {
      any_wrong = true;
    }
  }
  
  // If any chains were updated, we have to recurse on all of our
  // children, since any one of those chains might be shared by any of
  // our child nodes.
  if (any_wrong) {
    Down::const_iterator di;
    for (di = cdata->_down.begin(); di != cdata->_down.end(); ++di) {
      PandaNode *child_node = (*di).get_child();
      CDReader cdata_child(child_node->_cycler);
      child_node->fix_chain_lengths(cdata_child);
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::r_list_descendants
//       Access: Private
//  Description: The recursive implementation of ls().
////////////////////////////////////////////////////////////////////
void PandaNode::
r_list_descendants(ostream &out, int indent_level) const {
  write(out, indent_level);

  CDReader cdata(_cycler);
  Down::const_iterator di;
  for (di = cdata->_down.begin(); di != cdata->_down.end(); ++di) {
    (*di).get_child()->r_list_descendants(out, indent_level + 2);
  }

  // Also report the number of stashed nodes at this level.
  /*
  int num_stashed = get_num_stashed();
  if (num_stashed != 0) {
    indent(out, indent_level) << "(" << num_stashed << " stashed)\n";
  }
  */
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::register_with_read_factory
//       Access: Public, Static
//  Description: Tells the BamReader how to create objects of type
//               PandaNode.
////////////////////////////////////////////////////////////////////
void PandaNode::
register_with_read_factory() {
  BamReader::get_factory()->register_factory(get_class_type(), make_from_bam);
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::write_datagram
//       Access: Public, Virtual
//  Description: Writes the contents of this object to the datagram
//               for shipping out to a Bam file.
////////////////////////////////////////////////////////////////////
void PandaNode::
write_datagram(BamWriter *manager, Datagram &dg) {
  dg.add_string(get_name());

  manager->write_cdata(dg, _cycler);
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::make_from_bam
//       Access: Protected, Static
//  Description: This function is called by the BamReader's factory
//               when a new object of type PandaNode is encountered
//               in the Bam file.  It should create the PandaNode
//               and extract its information from the file.
////////////////////////////////////////////////////////////////////
TypedWritable *PandaNode::
make_from_bam(const FactoryParams &params) {
  PandaNode *node = new PandaNode("");
  DatagramIterator scan;
  BamReader *manager;

  parse_params(params, scan, manager);
  node->fillin(scan, manager);

  return node;
}

////////////////////////////////////////////////////////////////////
//     Function: PandaNode::fillin
//       Access: Protected
//  Description: This internal function is called by make_from_bam to
//               read in all of the relevant data from the BamFile for
//               the new PandaNode.
////////////////////////////////////////////////////////////////////
void PandaNode::
fillin(DatagramIterator &scan, BamReader *manager) {
  TypedWritable::fillin(scan, manager);

  string name = scan.get_string();
  set_name(name);

  manager->read_cdata(scan, _cycler);
}
