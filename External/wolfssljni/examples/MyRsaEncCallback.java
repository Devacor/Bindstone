/* MyRsaEncCallback.java
 *
 * Copyright (C) 2006-2018 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

import java.io.*;
import java.net.*;
import java.nio.*;
import com.wolfssl.*;
import com.wolfssl.wolfcrypt.*;

class MyRsaEncCallback implements WolfSSLRsaEncCallback
{
    public int rsaEncCallback(WolfSSLSession ssl, ByteBuffer in, long inSz,
            ByteBuffer out, int[] outSz, ByteBuffer keyDer, long keySz,
            Object ctx) {

        System.out.println("---------- Entering MyRsaEncCallback ----------");
        int ret = -1;

        RSA rsa = new RSA();
        MyRsaEncCtx rsaEncCtx = (MyRsaEncCtx)ctx;

        ret = rsa.doEnc(in, inSz, out, outSz, keyDer, keySz);
        if (ret != 0) {
            System.out.println("RSA encrypt failed in " +
                    "MyRsaEncCallback");
        }

        System.out.println("---------- Leaving MyRsaEncCallback ----------");
        return ret;
    }
}

