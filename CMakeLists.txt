if(CONFIG_MESHTASTIC_CLIENT)
  zephyr_library()
  zephyr_library_include_directories(include generated nanopb)
  zephyr_include_directories(include)
  file(GLOB protobuf_sources CONFIGURE_DEPENDS generated/meshtastic/*.pb.c)
  zephyr_library_sources(src/frame.c src/client.c src/tx.c src/diagnostics.c ${protobuf_sources}
    nanopb/pb_common.c nanopb/pb_encode.c nanopb/pb_decode.c)
endif()
