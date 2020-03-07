
package com.SDL2_GradleApp;

import android.os.Bundle;
import org.libsdl.app.SDLActivity;

public class SDL2_GradleApp extends SDLActivity
{
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
    }

	@Override
	protected String[] getLibraries() {
        return new String[] {
            "c++_shared",
            "m",
            "wolfssl",
            "boost_system",
            "boost_filesystem",
            //"hidapi", //I'm just compiling this directly into the app.
            "SDL2",
            "SDL2_Image",
            "SDL2_ttf",
            "mutedvision",
            //"external",
            "bindstonemain"
        };
    }
}
