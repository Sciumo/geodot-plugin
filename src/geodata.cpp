#include "geodata.h"
#include "geofeatures.h"
#include "godot_cpp/core/error_macros.hpp"
#include "raster-tile-extractor/RasterTileExtractor.h"
#include "vector-extractor/Feature.h"
#include "vector-extractor/VectorExtractor.h"
#include <cmath>

#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

GeoDataset::~GeoDataset() {
    // delete dataset;
}

void GeoDataset::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_valid"), &GeoDataset::is_valid);
    ClassDB::bind_method(D_METHOD("get_raster_layers"), &GeoDataset::get_raster_layers);
    ClassDB::bind_method(D_METHOD("get_feature_layers"), &GeoDataset::get_feature_layers);
    ClassDB::bind_method(D_METHOD("get_raster_layer", "name"), &GeoDataset::get_raster_layer);
    ClassDB::bind_method(D_METHOD("get_feature_layer", "name"), &GeoDataset::get_feature_layer);
    ClassDB::bind_method(D_METHOD("load_from_file", "file_path"), &GeoDataset::load_from_file);
}

bool GeoDataset::is_valid() {
    return dataset && dataset->is_valid();
}

Array GeoDataset::get_raster_layers() {
    Array layers = Array();

    std::vector<std::string> names = dataset->get_raster_layer_names();

    for (std::string name : names) {
        layers.append(get_raster_layer(name.c_str()));
    }

    return layers;
}

Array GeoDataset::get_feature_layers() {
    Array layers = Array();

    std::vector<std::string> names = dataset->get_feature_layer_names();

    for (std::string name : names) {
        layers.append(get_feature_layer(name.c_str()));
    }

    return layers;
}

Ref<GeoRasterLayer> GeoDataset::get_raster_layer(String name) {
    Ref<GeoRasterLayer> raster_layer;
    raster_layer.instantiate();

    raster_layer->set_native_dataset(dataset->get_subdataset(name.utf8().get_data()));
    raster_layer->set_name(name);
    raster_layer->set_origin_dataset(this);

    return raster_layer;
}

Ref<GeoFeatureLayer> GeoDataset::get_feature_layer(String name) {
    Ref<GeoFeatureLayer> feature_layer;
    feature_layer.instantiate();

    feature_layer->set_native_layer(dataset->get_layer(name.utf8().get_data()));
    feature_layer->set_name(name);
    feature_layer->set_origin_dataset(this);

    return feature_layer;
}

void GeoDataset::load_from_file(String file_path, bool write_access) {
    this->write_access = write_access;

    set_path(file_path);
    dataset = VectorExtractor::open_dataset(file_path.utf8().get_data(), write_access);
}

void GeoDataset::set_native_dataset(NativeDataset *new_dataset) {
    dataset = new_dataset;
}

void GeoFeatureLayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_valid"), &GeoFeatureLayer::is_valid);
    ClassDB::bind_method(D_METHOD("get_dataset"), &GeoFeatureLayer::get_dataset);
    ClassDB::bind_method(D_METHOD("get_feature_by_id"), &GeoFeatureLayer::get_feature_by_id);
    ClassDB::bind_method(D_METHOD("get_all_features"), &GeoFeatureLayer::get_all_features);
    ClassDB::bind_method(D_METHOD("get_features_near_position"),
                         &GeoFeatureLayer::get_features_near_position);
    ClassDB::bind_method(D_METHOD("create_feature"), &GeoFeatureLayer::create_feature);
    ClassDB::bind_method(D_METHOD("remove_feature"), &GeoFeatureLayer::remove_feature);
    ClassDB::bind_method(D_METHOD("save_override"), &GeoFeatureLayer::save_override);
    ClassDB::bind_method(D_METHOD("save_new"), &GeoFeatureLayer::save_new);

    ADD_SIGNAL(MethodInfo("feature_added", PropertyInfo(Variant::OBJECT, "new_feature")));
    ADD_SIGNAL(MethodInfo("feature_removed", PropertyInfo(Variant::OBJECT, "removed_feature")));
}

bool GeoFeatureLayer::is_valid() {
    return layer && layer->is_valid();
}

Ref<GeoDataset> GeoFeatureLayer::get_dataset() {
    return origin_dataset;
}

// Utility function for converting a Processing Library Feature to the appropriate GeoFeature
Ref<GeoFeature> get_specialized_feature(Feature *raw_feature) {
    // Check which geometry this feature has, and cast it to the according
    // specialized class
    if (raw_feature->geometry_type == raw_feature->POINT) {
        Ref<GeoPoint> point;
        point.instantiate();

        PointFeature *point_feature = dynamic_cast<PointFeature *>(raw_feature);

        point->set_gdal_feature(point_feature);

        return point;
    } else if (raw_feature->geometry_type == raw_feature->LINE) {
        Ref<GeoLine> line;
        line.instantiate();

        LineFeature *line_feature = dynamic_cast<LineFeature *>(raw_feature);

        line->set_gdal_feature(line_feature);

        return line;
    } else if (raw_feature->geometry_type == raw_feature->POLYGON) {
        Ref<GeoPolygon> polygon;
        polygon.instantiate();

        PolygonFeature *polygon_feature = dynamic_cast<PolygonFeature *>(raw_feature);

        polygon->set_gdal_feature(polygon_feature);

        return polygon;
    } else {
        // Geometry type is NONE or unknown
        Ref<GeoFeature> feature;
        feature.instantiate();

        feature->set_gdal_feature(raw_feature);

        return feature;
    }
}

Ref<GeoFeature> GeoFeatureLayer::get_feature_by_id(int id) {
    std::list<Feature *> features = layer->get_feature_by_id(id);
    if (features.empty()) return nullptr;

    // TODO: How to deal with MultiFeatures? Currently we just use the first one
    Ref<GeoFeature> feature = get_specialized_feature(features.front());

    return feature;
}

Array GeoFeatureLayer::get_all_features() {
    Array geofeatures = Array();

    std::list<Feature *> gdal_features = layer->get_features();
    Array features = Array();

    for (Feature *raw_feature : gdal_features) {
        features.push_back(get_specialized_feature(raw_feature));
    }

    return features;
}

Ref<GeoFeature> GeoFeatureLayer::create_feature() {
    Feature *gdal_feature = layer->create_feature();

    Ref<GeoFeature> feature = get_specialized_feature(gdal_feature);

    emit_signal("feature_added", feature);
    return feature;
}

void GeoFeatureLayer::remove_feature(Ref<GeoFeature> feature) {
    // Mark the feature for deletion
    feature->set_deleted(true);

    emit_signal("feature_removed", feature);
}

void GeoFeatureLayer::save_override() {
    ERR_FAIL_COND_MSG(!origin_dataset->write_access,
                      "Cannot override a layer whose dataset was not opened with write access!");
    layer->save_override();
}

void GeoFeatureLayer::save_new(String file_path) {
    layer->save_modified_layer(file_path.utf8().get_data());
}

Array GeoFeatureLayer::get_features_near_position(double pos_x, double pos_y, double radius,
                                                  int max_features) {
    Array features = Array();

    std::list<Feature *> raw_features =
        layer->get_features_near_position(pos_x, pos_y, radius, max_features);

    for (Feature *raw_feature : raw_features) {
        features.push_back(get_specialized_feature(raw_feature));
    }

    return features;
}

Array GeoFeatureLayer::crop_lines_to_square(double top_left_x, double top_left_y,
                                            double size_meters, int max_lines) {
    // TODO
    return Array();
}

void GeoFeatureLayer::set_native_layer(NativeLayer *new_layer) {
    layer = new_layer;
}

void GeoFeatureLayer::set_origin_dataset(Ref<GeoDataset> dataset) {
    this->origin_dataset = dataset;
}

GeoRasterLayer::~GeoRasterLayer() {
    // delete dataset;
}

void GeoRasterLayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_valid"), &GeoRasterLayer::is_valid);
    ClassDB::bind_method(D_METHOD("get_dataset"), &GeoRasterLayer::get_dataset);
    ClassDB::bind_method(D_METHOD("get_image"), &GeoRasterLayer::get_image);
    ClassDB::bind_method(D_METHOD("get_value_at_position"), &GeoRasterLayer::get_value_at_position);
    ClassDB::bind_method(D_METHOD("get_value_at_position_with_resolution"),
                         &GeoRasterLayer::get_value_at_position_with_resolution);
    ClassDB::bind_method(D_METHOD("set_value_at_position"), &GeoRasterLayer::set_value_at_position);
    ClassDB::bind_method(D_METHOD("smooth_add_value_at_position"),
                         &GeoRasterLayer::smooth_add_value_at_position);
    ClassDB::bind_method(D_METHOD("overlay_image_at_position"),
                         &GeoRasterLayer::overlay_image_at_position);
    ClassDB::bind_method(D_METHOD("get_extent"), &GeoRasterLayer::get_extent);
    ClassDB::bind_method(D_METHOD("get_center"), &GeoRasterLayer::get_center);
    ClassDB::bind_method(D_METHOD("get_min"), &GeoRasterLayer::get_min);
    ClassDB::bind_method(D_METHOD("get_max"), &GeoRasterLayer::get_max);
    ClassDB::bind_method(D_METHOD("clone"), &GeoRasterLayer::clone);
    ClassDB::bind_method(D_METHOD("load_from_file", "file_path"), &GeoRasterLayer::load_from_file);
}

bool GeoRasterLayer::is_valid() {
    return dataset && dataset->is_valid();
}

Ref<GeoDataset> GeoRasterLayer::get_dataset() {
    return origin_dataset;
}

Ref<GeoImage> GeoRasterLayer::get_image(double top_left_x, double top_left_y, double size_meters,
                                        int img_size, int interpolation_type) {

    Ref<GeoImage> image;
    image.instantiate();

    if (dataset == nullptr || !dataset->is_valid()) {
        // TODO: Set validity to false
        UtilityFunctions::push_error("Raster layer '", get_name(),
                                     "' is invalid, cannot perform get_image!");
        return image;
    }

    GeoRaster *raster = RasterTileExtractor::get_tile_from_dataset(
        dataset->dataset, top_left_x, top_left_y, size_meters, img_size, interpolation_type);

    if (raster == nullptr) {
        // TODO: Set validity to false
        UtilityFunctions::push_error("No valid data was available in the raster layer '",
                                     get_name(), "' at the requested position position!");
        return image;
    }

    image->set_raster(raster, interpolation_type);

    return image;
}

float GeoRasterLayer::get_value_at_position(double pos_x, double pos_y) {
    // 0.0001 meters are used for the size because it can't be 0, but should be a pinpoint value.
    return get_value_at_position_with_resolution(pos_x, pos_y, 0.0001);
}

float GeoRasterLayer::get_value_at_position_with_resolution(double pos_x, double pos_y,
                                                            double pixel_size_meters) {
    // TODO: Figure out what exactly we need to clamp to for precise values
    // pos_x -= std::fmod(pos_x, pixel_size_meters);
    // pos_y -= std::fmod(pos_y, pixel_size_meters);

    // Get the GeoRaster for this position with a resolution of 1x1px.
    GeoRaster *raster = RasterTileExtractor::get_tile_from_dataset(dataset->dataset, pos_x, pos_y,
                                                                   pixel_size_meters, 1, 1);

    // TODO: Currently only implemented for RF type.
    // For others, we would either need a completely generic return value, or other specific
    // functions (as the user likely knows or wants to know the exact type).
    if (raster->get_format() == GeoRaster::FORMAT::RF) {
        float *array = (float *)raster->get_as_array();

        return array[0];
    }

    return -1.0;
}

void GeoRasterLayer::set_value_at_position(double pos_x, double pos_y, Variant value) {}

void GeoRasterLayer::smooth_add_value_at_position(double pos_x, double pos_y, double summand,
                                                  double radius) {}

void GeoRasterLayer::overlay_image_at_position(double pos_x, double pos_y, Ref<Image> image,
                                               double scale) {}

Rect2 GeoRasterLayer::get_extent() {
    return Rect2(extent_data.left, extent_data.top, extent_data.right - extent_data.left,
                 extent_data.down - extent_data.top);
}

Vector3 GeoRasterLayer::get_center() {
    return Vector3(extent_data.left + (extent_data.right - extent_data.left) / 2.0, 0.0,
                   extent_data.top + (extent_data.down - extent_data.top) / 2.0);
}

float GeoRasterLayer::get_min() {
    return RasterTileExtractor::get_min(dataset->dataset);
}

float GeoRasterLayer::get_max() {
    return RasterTileExtractor::get_max(dataset->dataset);
}

void GeoRasterLayer::set_origin_dataset(Ref<GeoDataset> dataset) {
    this->origin_dataset = dataset;
}

Ref<GeoRasterLayer> GeoRasterLayer::clone() {
    Ref<GeoRasterLayer> layer_clone;
    layer_clone.instantiate();

    layer_clone->set_native_dataset(dataset->clone());
    layer_clone->set_origin_dataset(origin_dataset);
    layer_clone->set_name(get_name());

    return layer_clone;
}

void GeoRasterLayer::load_from_file(String file_path, bool write_access) {
    this->write_access = write_access;

    dataset = VectorExtractor::open_dataset(file_path.utf8().get_data(), write_access);

    // TODO: Might be better to produce a hard crash here, but CRASH_COND doesn't have the desired
    // effect - see https://github.com/godotengine/godot-cpp/issues/521
    if (!dataset->is_valid()) {
        UtilityFunctions::push_error("Could not load GeoRasterLayer from path '", file_path, "'!");
    }
}

void GeoRasterLayer::set_native_dataset(NativeDataset *new_dataset) {
    dataset = new_dataset;
    extent_data = RasterTileExtractor::get_extent_data(new_dataset->dataset);
}

} // namespace godot