package com.google.android.apps.internal.games.memoryadvice;

import static com.google.android.apps.internal.games.memoryadvice_common.ConfigUtils.getMemoryQuantity;
import static com.google.android.apps.internal.games.memoryadvice_common.ConfigUtils.getOrDefault;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.DeadObjectException;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.util.Log;
import java.util.Timer;
import java.util.TimerTask;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * A system to discover the limits of the device using a service running in a different process.
 * The limits are compatible with the lab test stress results.
 */
class OnDeviceStressTester {
  // Message codes for inter-process communication.
  public static final int GET_BASELINE_METRICS = 1;
  public static final int GET_BASELINE_METRICS_RETURN = 2;
  public static final int OCCUPY_MEMORY = 3;
  public static final int OCCUPY_MEMORY_OK = 4;
  public static final int OCCUPY_MEMORY_FAILED = 5;
  public static final int SERVICE_CHECK_FREQUENCY = 250;
  public static final int NO_PID_TIMEOUT = 1000 * 2;
  private static final String TAG = OnDeviceStressTester.class.getSimpleName();
  private final ServiceConnection serviceConnection;

  /**
   * Construct an on-device stress tester.
   *
   * @param context  The current context.
   * @param params   The configuration parameters for the tester.
   * @param consumer The handler to call back when the test is complete.
   */
  OnDeviceStressTester(Context context, JSONObject params, Consumer consumer) {
    Intent launchIntent = new Intent(context, StressService.class);
    launchIntent.putExtra("params", params.toString());
    serviceConnection = new ServiceConnection() {
      private Messenger messenger;
      private JSONObject baseline;
      private JSONObject limit;
      private Timer serviceWatcherTimer;
      private int connectionCount;

      public void onServiceConnected(ComponentName name, IBinder service) {
        if (connectionCount > 0) {
          // If the service connects after the first time it is because the service was killed
          // on the first occasion.
          stopService();
          return;
        }
        connectionCount++;
        messenger = new Messenger(service);

        {
          // Ask the service for its pid and baseline metrics.
          Message message = Message.obtain(null, GET_BASELINE_METRICS, 0, 0);
          message.replyTo = new Messenger(new Handler(new Handler.Callback() {
            @Override
            public boolean handleMessage(Message msg) {
              if (msg.what == GET_BASELINE_METRICS_RETURN) {
                Bundle bundle = (Bundle) msg.obj;
                try {
                  baseline = new JSONObject(bundle.getString("metrics"));
                } catch (JSONException e) {
                  throw new MemoryAdvisorException(e);
                }
                int servicePid = msg.arg1;
                serviceWatcherTimer = new Timer();
                serviceWatcherTimer.scheduleAtFixedRate(new TimerTask() {
                  private long lastSawPid;
                  @Override
                  public void run() {
                    int oomScore = Utils.getOomScore(servicePid);
                    if (oomScore == -1) {
                      if (lastSawPid > 0
                          && System.currentTimeMillis() - lastSawPid > NO_PID_TIMEOUT) {
                        // The prolonged lack of OOM score is almost certainly because the process
                        // was killed.
                        stopService();
                      }
                    } else {
                      lastSawPid = System.currentTimeMillis();
                    }
                  }
                }, 0, SERVICE_CHECK_FREQUENCY);
              } else {
                throw new MemoryAdvisorException("Unexpected return code " + msg.what);
              }
              return true;
            }
          }));
          sendMessage(message);
        }
        try {
          int toAllocate = (int) getMemoryQuantity(
              getOrDefault(params.getJSONObject("onDeviceStressTest"), "segmentSize", "4M"));
          Message message = Message.obtain(null, OCCUPY_MEMORY, toAllocate, 0);
          message.replyTo = new Messenger(new Handler(new Handler.Callback() {
            @Override
            public boolean handleMessage(Message msg) {
              // The service's return message includes metrics.
              Bundle bundle = (Bundle) msg.obj;
              try {
                JSONObject received = new JSONObject(bundle.getString("metrics"));
                if (limit == null
                    || limit.getJSONObject("meta").getLong("time")
                        < received.getJSONObject("meta").getLong("time")) {
                  consumer.progress(received);
                  // Metrics can be received out of order. The latest only is recorded as the limit.
                  limit = received;
                }
              } catch (JSONException e) {
                throw new MemoryAdvisorException(e);
              }
              if (msg.what == OCCUPY_MEMORY_FAILED) {
                stopService();
              } else if (msg.what != OCCUPY_MEMORY_OK) {
                throw new MemoryAdvisorException("Unexpected return code " + msg.what);
              }
              return true;
            }
          }));
          sendMessage(message);
        } catch (JSONException e) {
          throw new IllegalStateException(e);
        }
      }

      public void onServiceDisconnected(ComponentName name) {
        messenger = null;
      }

      private void sendMessage(Message message) {
        try {
          messenger.send(message);
        } catch (DeadObjectException e) {
          // Complete the test because the messenger could not find the service - probably because
          // it was terminated by the system.
          stopService();
        } catch (RemoteException e) {
          Log.e(TAG, "Error sending message", e);
        }
      }

      private void stopService() {
        serviceWatcherTimer.cancel();
        context.stopService(launchIntent);
        context.unbindService(serviceConnection);
        consumer.accept(baseline, limit);
      }
    };

    context.startService(launchIntent);
    context.bindService(launchIntent, serviceConnection, Context.BIND_AUTO_CREATE);
  }

  interface Consumer {
    void progress(JSONObject metrics);
    void accept(JSONObject baseline, JSONObject limit);
  }
}
