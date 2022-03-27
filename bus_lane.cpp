#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <cuda_runtime_api.h>
#include "gstnvdsmeta.h"

#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

using namespace cv;
using namespace std;

#define MUXER_OUTPUT_WIDTH 1920
#define MUXER_OUTPUT_HEIGHT 1080

#define MUXER_BATCH_TIMEOUT_USEC 40000

void cb_newpad(GstElement *decodebin, GstPad *decoder_src_pad, gpointer data) {
    g_print("In cb_newpad\n");
    GstCaps *caps = gst_pad_get_current_caps(decoder_src_pad);
    const GstStructure *str = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(str);
    GstElement *source_bin = (GstElement *)data;
    GstCapsFeatures *features = gst_caps_get_features(caps, 0);

    /* Need to check if the pad created by the decodebin is for video and not
    * audio. */
    if (!strncmp(name, "video", 5)) {
      /* Link the decodebin pad only if decodebin has picked nvidia
      * decoder plugin nvdec_*. We do this by checking if the pad caps contain
      * NVMM memory features. */
      if (gst_caps_features_contains(features, "memory:NVMM")) {
        /* Get the source bin ghost pad */
        GstPad *bin_ghost_pad = gst_element_get_static_pad(source_bin, "src");
        if (!gst_ghost_pad_set_target(GST_GHOST_PAD(bin_ghost_pad),
                                      decoder_src_pad)) {
          g_printerr("Failed to link decoder src pad to source bin ghost pad\n");
        }
        gst_object_unref(bin_ghost_pad);
      }
      else {
        g_printerr("Error: Decodebin did not pick nvidia decoder plugin.\n");
      }
    }
  }

void decodebin_child_added(GstChildProxy *child_proxy, GObject *object, gchar *name, gpointer user_data) {
    g_print("Decodebin child added: %s\n", name);
    if (g_strrstr(name, "decodebin") == name) {
      g_signal_connect(G_OBJECT(object), "child-added",
                      G_CALLBACK(decodebin_child_added), user_data);
    }
    if (g_strstr_len(name, -1, "nvv4l2decoder") == name) {
      g_print("Seting bufapi_version\n");
      g_object_set(object, "bufapi-version", TRUE, NULL);
    }
  }

static GstPadProbeReturn
osd_sink_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{

    //ROI for sample video
    vector<Point> vert(5);
    vert[0] = Point( 840, 8 );
    vert[1] = Point( 964, 8 );
    vert[2] = Point( 1000, 120 );
    vert[3] = Point( 1680, 1076 );
    vert[4] = Point( 668, 1076 );

    Point obj_cen;
    int flag;

    GstBuffer *buf = (GstBuffer *) info->data;
    NvDsObjectMeta *obj_meta = NULL;
    NvDsMetaList * l_frame = NULL;
    NvDsMetaList * l_obj = NULL;
    NvDsDisplayMeta *display_meta = NULL;
    NvOSD_LineParams  *line_params;
    vector<NvDsObjectMeta *> rem;

    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);

    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {

        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) (l_frame->data);

        for (l_obj = frame_meta->obj_meta_list; l_obj != NULL;
                l_obj = l_obj->next) {

            obj_meta = (NvDsObjectMeta *) (l_obj->data);

            if (obj_meta->class_id == 0)
              rem.insert(rem.begin(),obj_meta);
          }

        for (auto it = rem.begin(); it != rem.end(); ++it)
          nvds_remove_obj_meta_from_frame (frame_meta, *it);


        for (l_obj = frame_meta->obj_meta_list; l_obj != NULL;
                l_obj = l_obj->next) {
            obj_meta = (NvDsObjectMeta *) (l_obj->data);

            obj_meta->rect_params.border_color.red = 0.0;
            obj_meta->rect_params.border_color.green = 1.0;
            obj_meta->rect_params.border_color.blue = 0.0;
            obj_meta->rect_params.border_color.alpha = 1.0;

            if ((obj_meta->class_id == 1)||(obj_meta->class_id == 2)||(obj_meta->class_id == 3)||(obj_meta->class_id == 7)) {
              obj_cen.x=int(obj_meta->rect_params.left+((obj_meta->rect_params.width)/2));
              obj_cen.y=int(obj_meta->rect_params.top+((obj_meta->rect_params.height)/2));

              flag = pointPolygonTest( vert, obj_cen, false );
              
              if (flag>0){
                obj_meta->rect_params.border_color.red = 1.0;
                obj_meta->rect_params.border_color.green = 0.0;
              }
            }
        }
        display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
        display_meta->num_labels = 1;
        for (int i=0; i<vert.size(); i++)
        {
          line_params = &display_meta->line_params[i];
          line_params->x1=vert[i].x;
          line_params->y1=vert[i].y;
          if (i!=vert.size()-1)
          {
            line_params->x2=vert[i+1].x;
            line_params->y2=vert[i+1].y;
          }
          else
          {
            line_params->x2=vert[0].x;
            line_params->y2=vert[0].y;
          }
          line_params->line_width = 2;
          line_params->line_color = (NvOSD_ColorParams) { 0.0, 0.0, 1.0, 1.0 };
          display_meta->num_lines+=1;
        
        }
        nvds_add_display_meta_to_frame(frame_meta, display_meta);
    }    
    return GST_PAD_PROBE_OK;
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;
      gst_message_parse_error (msg, &error, &debug);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      if (debug)
        g_printerr ("Error details: %s\n", debug);
      g_free (debug);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{

  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL, *bin = NULL, *uri_decode_bin = NULL, *source_bin = NULL, *h264parser = NULL,
      *decoder = NULL, *streammux = NULL, *sink = NULL, *pgie = NULL, *nvvidconv = NULL,
      *nvosd = NULL;

  GstElement *transform = NULL;
  GstBus *bus = NULL;
  guint bus_watch_id;
  GstPad *osd_sink_pad = NULL;
  
  gchar bin_name[16] = {};

  int current_device = -1;
  cudaGetDevice(&current_device);
  struct cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, current_device);
  /* Check input arguments */
  if (argc != 2) {
    g_printerr ("Usage: %s <absolute filepath>\n", argv[0]);
    return -1;
  }

  /* Standard GStreamer initialization */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* Create gstreamer elements */
  /* Create Pipeline element that will form a connection of other elements */
  pipeline = gst_pipeline_new ("bus_lane-pipeline");

  /* Create nvstreammux instance to form batches from one or more sources. */
  streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

  if (!pipeline || !streammux) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  gst_bin_add(GST_BIN(pipeline), streammux);
  
  source_bin = gst_bin_new(bin_name);
  
  uri_decode_bin = gst_element_factory_make("uridecodebin", "uri-decode-bin");
  
  if (!source_bin || !uri_decode_bin) {
    g_printerr("One element in source bin could not be created.\n");
    return -1;
  }

  g_object_set(G_OBJECT(uri_decode_bin), "uri", (gchar *)argv[1], NULL);
  
  
  g_signal_connect(G_OBJECT(uri_decode_bin), "pad-added",G_CALLBACK(cb_newpad), source_bin);
  
  g_signal_connect(G_OBJECT(uri_decode_bin), "child-added",G_CALLBACK(decodebin_child_added), source_bin);
  
  gst_bin_add(GST_BIN(source_bin), uri_decode_bin);
  
  if (!gst_element_add_pad(source_bin, gst_ghost_pad_new_no_target("src",GST_PAD_SRC))) {
    g_printerr("Failed to add ghost pad in source bin\n");
    return -1;
  }
  
  if (!source_bin) {
    g_printerr("Failed to create source bin. Exiting.\n");
    return -1;
  }

  gst_bin_add(GST_BIN(pipeline), source_bin);
  
  GstPad *sinkpad, *srcpad;
  gchar pad_name_sink[16] = "sink_0";
  gchar pad_name_src[16] = "src";  
  
  sinkpad = gst_element_get_request_pad (streammux, pad_name_sink);
  if (!sinkpad) {
    g_printerr ("Streammux request sink pad failed. Exiting.\n");
    return -1;
  }

  srcpad = gst_element_get_static_pad (source_bin, pad_name_src);
  if (!srcpad) {
    g_printerr ("Decoder request src pad failed. Exiting.\n");
    return -1;
  }
  
  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
      g_printerr ("Failed to link decoder to stream muxer. Exiting.\n");
      return -1;
  }

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);


  /* Use nvinfer to run inferencing on decoder's output,
   * behaviour of inferencing is set through config file */
  pgie = gst_element_factory_make ("nvinfer", "primary-nvinference-engine");

  /* Use convertor to convert from NV12 to RGBA as required by nvosd */
  nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

  /* Create OSD to draw on the converted RGBA buffer */
  nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

  /* Finally render the osd output */
  if(prop.integrated) {
    transform = gst_element_factory_make ("nvegltransform", "nvegl-transform");
  }
  sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");

  if (!source_bin || !pgie
      || !nvvidconv || !nvosd || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  if(!transform && prop.integrated) {
    g_printerr ("One tegra element could not be created. Exiting.\n");
    return -1;
  }

  g_object_set (G_OBJECT (streammux), "batch-size", 1, NULL);

  g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
      MUXER_OUTPUT_HEIGHT,
      "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, NULL);

  /* Set all the necessary properties of the nvinfer element,
   * the necessary ones are : */
  g_object_set (G_OBJECT (pgie),
      "config-file-path", "bus_lane_pgie_config.txt", NULL);

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* Set up the pipeline */
  /* we add all elements into the pipeline */
  if(prop.integrated) {
    gst_bin_add_many (GST_BIN (pipeline),
        source_bin, streammux, pgie,
        nvvidconv, nvosd, transform, sink, NULL);
  }
  else {
  gst_bin_add_many (GST_BIN (pipeline),
      source_bin, streammux, pgie,
      nvvidconv, nvosd, sink, NULL);
  }

  if(prop.integrated) {
    if (!gst_element_link_many (streammux, pgie,
        nvvidconv, nvosd, transform, sink, NULL)) {
      g_printerr ("Elements could not be linked: 2. Exiting.\n");
      return -1;
    }
  }
  else {
    if (!gst_element_link_many (streammux, pgie,
        nvvidconv, nvosd, sink, NULL)) {
      g_printerr ("Elements could not be linked: 2. Exiting.\n");
      return -1;
    }
  }

  /* Lets add probe to get informed of the meta data generated, we add probe to
   * the sink pad of the osd element, since by that time, the buffer would have
   * had got all the metadata. */
  osd_sink_pad = gst_element_get_static_pad (nvosd, "sink");
  if (!osd_sink_pad)
    g_print ("Unable to get sink pad\n");
  else
    gst_pad_add_probe (osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
        osd_sink_pad_buffer_probe, NULL, NULL);
  gst_object_unref (osd_sink_pad);

  /* Set the pipeline to "playing" state */
  g_print ("Now playing: %s\n", argv[1]);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait till pipeline encounters an error or EOS */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);
  return 0;
}
