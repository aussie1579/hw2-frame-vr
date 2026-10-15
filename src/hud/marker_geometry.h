#pragma once

#include <cstddef>

// Where on the screen a 2D HUD draw lands, from the CPU side: the game streams its HUD vertices with glBufferData
// right before drawing (build 12653478: position at attribute 0, 3 floats, in the ortho [0,1] screen space), so
// small buffers are shadowed here and each draw's positions are read back through its VAO's attribute-0 pointer.
// Used to anchor world-space markers (selection circles, brackets, health bars, names) at the depth of the ship
// under them. Tracking is always on: VAOs are set up long before VR starts.
namespace hw2vr::markers {

// From the GL hooks (proxy/gl_record_hooks.cpp), game calls only.
void on_bind_vertex_array(unsigned vao);
void on_bind_buffer(unsigned target, unsigned buffer);
void on_vertex_attrib_pointer(unsigned index, int size, unsigned type, int stride, const void* pointer);
void on_buffer_data(unsigned target, std::ptrdiff_t size, const void* data);
void on_buffer_sub_data(unsigned target, std::ptrdiff_t offset, std::ptrdiff_t size, const void* data);
void on_delete_buffers(int n, const unsigned* names);
void on_delete_vertex_arrays(int n, const unsigned* names);

// Screen rectangle {x0, y0, x1, y1} in [0,1] (x right, y up, as the HUD's ortho maps it) of a draw through the
// current VAO after mv and proj. elements: glDrawElements (count indices of index_type at byte offset first);
// otherwise glDrawArrays (count vertices from first). False if the vertices aren't known (not shadowed, not float).
bool screen_bounds(bool elements, std::ptrdiff_t first, int count, unsigned index_type, const float mv[16],
                   const float proj[16], float out[4]);

}  // namespace hw2vr::markers
