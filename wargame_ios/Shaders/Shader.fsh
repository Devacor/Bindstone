//
//  Shader.fsh
//  wargame_ios
//
//  Created by Michael Hamilton on 2013-01-15.
//  Copyright (c) 2013 Michael Hamilton. All rights reserved.
//

varying lowp vec4 colorVarying;

void main()
{
    gl_FragColor = colorVarying;
}
