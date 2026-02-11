add_library(Json IMPORTED INTERFACE)
set_target_properties(Json PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/modules/json/include"
)
