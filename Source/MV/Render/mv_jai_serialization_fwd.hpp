#pragma once
#ifndef MV_JAI_SERIALIZATION_FWD_HPP
#define MV_JAI_SERIALIZATION_FWD_HPP

// JaiScript archives now use CRTP (no virtual dispatch) and all save/load
// functions are templated. Forward declarations aren't useful for templates
// since definitions must be visible at instantiation sites.
//
// MV type serialization is implemented:
//   - Most types (Point, Size, Color, BoxAABB, etc.): member serialize() found via ADL
//   - Anchors: inline templated save/load in drawable.h
//   - Other Scene types: inline templated in their respective headers
//
// Include the specific headers directly when serialization is needed.

#endif // MV_JAI_SERIALIZATION_FWD_HPP
