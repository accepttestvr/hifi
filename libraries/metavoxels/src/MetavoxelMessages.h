//
//  MetavoxelMessages.h
//  metavoxels
//
//  Created by Andrzej Kapolka on 12/31/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#ifndef __interface__MetavoxelMessages__
#define __interface__MetavoxelMessages__

#include "Bitstream.h"

/// A message containing the state of a client.
class ClientStateMessage {
    STREAMABLE
    
public:
    
    STREAM glm::vec3 position;
};

DECLARE_STREAMABLE_METATYPE(ClientStateMessage)

#endif /* defined(__interface__MetavoxelMessages__) */
