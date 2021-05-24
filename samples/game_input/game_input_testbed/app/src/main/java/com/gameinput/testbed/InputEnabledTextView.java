/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.gameinput.testbed;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.inputmethod.EditorInfo;

import com.google.androidgamesdk.gameinput.GameInput;
import com.google.androidgamesdk.gameinput.InputConnection;
import com.google.androidgamesdk.gameinput.Listener;
import com.google.androidgamesdk.gameinput.Settings;
import com.google.androidgamesdk.gameinput.State;

import static android.view.inputmethod.EditorInfo.IME_ACTION_NONE;

public class InputEnabledTextView extends View implements Listener {
    public InputConnection mInputConnection;

    public InputEnabledTextView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public InputEnabledTextView(Context context) {
        super(context);
    }

    public void createInputConnection(int inputType) {
        EditorInfo editorInfo = new EditorInfo();
        // Note that if you use TYPE_CLASS_TEXT, the IME may fill the whole window because we
        // are in landscape and the events aren't reflected back to it, so you can't see what
        // you're typing. This needs fixing.
        editorInfo.inputType = inputType;
        editorInfo.actionId = IME_ACTION_NONE;
        mInputConnection = new InputConnection(
                this.getContext(),
                this,
                new Settings(editorInfo, true)
        ).setListener(this);
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        if (outAttrs != null) {
            GameInput.copyEditorInfo(mInputConnection.getEditorInfo(), outAttrs);
        }
        return mInputConnection;
    }

    // Called when the IME has changed the input
    @Override
    public void stateChanged(State newState, boolean dismissed) {
        System.out.println("stateChanged: " + newState + " dismissed: " + dismissed);
        onTextInputEventNative(newState);
    }

    private native void onTextInputEventNative(State softKeyboardEvent);
}