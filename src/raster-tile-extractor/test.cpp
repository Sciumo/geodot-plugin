// Used for testing the libRasterTileExtractor library.

#include <iostream>
#include "RasterTileExtractor.h"

int main() {
    RasterTileExtractor::initialize();

    float new_top_left_x = 1546670.0;
    float new_top_left_y = 5918250.0;
    float new_size = 50000.0;
    int img_size = 256;

    GeoRaster *raster = RasterTileExtractor::get_raster_at_position("/home/retour/LandscapeLab/testdata/DGM_K_5m", "tif", new_top_left_x, new_top_left_y, new_size, img_size, 0);
    std::cout << ((float *)raster->get_as_array())[0] << std::endl;

    return 0;
}