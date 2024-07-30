if(NOT COMPILED_FOR_TARGET)
# Build for testing on host.
target_sources(${PROJECT_NAME} PRIVATE
    
)
else()
# Build for embedded target
target_sources(${PROJECT_NAME} PRIVATE
SysTickTimeSource.cpp
)
endif()