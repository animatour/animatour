# Animatour

## About

Animatour (formerly, yet briefly, AnimaTrip) is a low-latency multi-machine video system used for effortlessly compositing video sources, such as webcams, with video inputs and the composite video output delivered over the Internet.

Animatour is influenced by [JackTrip](https://github.com/jacktrip/jacktrip) and powered by [GStreamer](https://gstreamer.freedesktop.org/).

Animatour is based on C++17, GStreamer, UDP communication, and the client-server model.

Animatour is licensed under the terms of the GNU General Public License v3.0 or later.

Animatour is in the alpha phase. The Animatour client and server are currently separate and Linux-only applications.

## Architecture

```mermaid
graph TD
    subgraph server[Server]
        udp-receiver[UDP Receiver]-->udp-router[UDP Router]
        subgraph gst-composite[GStreamer Composite Pipeline]
            udpsrc-1[udpsrc 1]-->udpsrc-1-rtph264depay[rtph264depay]-->udpsrc-1-avdec_h264[avdec_h264]-->udpsrc-1-videoscale[videoscale]-->udpsrc-1-videoconvert[videoconvert]-->compositor
            udpsrc-2[udpsrc 2]-->udpsrc-2-rtph264depay[rtph264depay]-->udpsrc-2-avdec_h264[avdec_h264]-->udpsrc-2-videoscale[videoscale]-->udpsrc-2-videoconvert[videoconvert]-->compositor
            udpsrc-N[udpsrc N]-->udpsrc-N-rtph264depay[rtph264depay]-->udpsrc-N-avdec_h264[avdec_h264]-->udpsrc-N-videoscale[videoscale]-->udpsrc-N-videoconvert[videoconvert]-->compositor
            compositor-->compositor-videobox[videobox]-->compositor-x264enc[x264enc]-->compositor-rtph264pay[rtph264pay]-->server-udpsink[udpsink]
        end
        udp-router-->udpsrc-1
        udp-router-->udpsrc-2
        udp-router-->udpsrc-N
        server-udpsink-->udp-sender[UDP Sender]
    end

    subgraph client-i[Client i]
        subgraph gst-capture[GStreamer Capture Pipeline]
            capture-v4l2src[v4l2src]-->capture-videoconvert[videoconvert]-->capture-videoscale[videoscale]-->capture-x264enc[x264enc]-->capture-rtph264pay[rtph264pay]-->capture-udpsink[udpsink]
        end
        subgraph gst-playback[GStreamer Playback Pipeline]
            playback-udpsrc[udpsrc]-->playback-rtph264depay[rtph264depay]-->playback-avdec_h264[avdec_h264]-->playback-videoconvert[videoconvert]
        end
        webcam[Webcam]-->capture-v4l2src
        playback-videoconvert-->display[Display]
    end

    udp-sender--UDP-->playback-udpsrc
    udp-sender--UDP-->client-1[Client 1]
    udp-sender--UDP-->client-2[Client 2]
    udp-sender--UDP-->client-N[Client N]
    capture-udpsink--UDP-->udp-receiver
    client-1--UDP-->udp-receiver
    client-2--UDP-->udp-receiver
    client-N--UDP-->udp-receiver
```

## Usage

### Animatour Server

#### Help

```bash
./animatour-server -h
# Usage: ./animatour-server [-p port]
```

#### Run Server

```bash
./animatour-server
```

### Animatour Client

#### Help

```bash
./animatour-client -h
# Usage: ./animatour-client [-r] [-t] [-d device] [-p serverport] [serverhost]
```

#### Run Webcam Client to Local Server

```bash
./animatour-client
```

or

```bash
./animatour-client -d /dev/video0
```

You may run multiple webcam clients on a single machine, but only one webcam client per webcam.

#### Run Test Client to Local Server

```bash
./animatour-client -t
```

You may run multiple test clients on a single machine.

#### Run Receive-Only (Sink-Only) Client to Local Server

```bash
./animatour-client -r
```

You may run multiple receive-only clients on a single machine.
