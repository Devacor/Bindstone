/* MyAtomicDecCtx.java
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
import javax.crypto.Cipher;
import com.wolfssl.*;

class MyAtomicDecCtx
{
    private boolean isCipherSetup = false;
    private Cipher cipher = null;

    public MyAtomicDecCtx() {
    }

    public void setCipher(Cipher cipher) {
        this.cipher = cipher;
    }

    public Cipher getCipher() {
        return this.cipher;
    }

    public boolean isCipherSetup() {
        return this.isCipherSetup;
    }

    public void isCipherSetup(boolean flag) {
        this.isCipherSetup = flag;
    }
}

