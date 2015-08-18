// mapnik
#include <mapnik/version.hpp>
#include <mapnik/unicode.hpp>
#include <mapnik/feature_factory.hpp>
#include <mapnik/memory_datasource.hpp>
#include <mapnik/value_types.hpp>

#include "mapnik_memory_datasource.hpp"
#include "mapnik_featureset.hpp"
#include "utils.hpp"
#include "ds_emitter.hpp"

// stl
#include <exception>

Nan::Persistent<FunctionTemplate> MemoryDatasource::constructor;

void MemoryDatasource::Initialize(Local<Object> target) {

    Nan::HandleScope scope;

    Local<FunctionTemplate> lcons = Nan::New<FunctionTemplate>(MemoryDatasource::New);
    lcons->InstanceTemplate()->SetInternalFieldCount(1);
    lcons->SetClassName(Nan::New("MemoryDatasource").ToLocalChecked());

    // methods
    Nan::SetPrototypeMethod(lcons, "parameters", parameters);
    Nan::SetPrototypeMethod(lcons, "describe", describe);
    Nan::SetPrototypeMethod(lcons, "featureset", featureset);
    Nan::SetPrototypeMethod(lcons, "add", add);
    Nan::SetPrototypeMethod(lcons, "fields", fields);

    target->Set(Nan::New("MemoryDatasource").ToLocalChecked(), lcons->GetFunction());
    constructor.Reset(lcons);
}

MemoryDatasource::MemoryDatasource() :
    Nan::ObjectWrap(),
    datasource_(),
    feature_id_(1),
    tr_("utf8") {}

MemoryDatasource::~MemoryDatasource()
{
}

NAN_METHOD(MemoryDatasource::New)
{
    if (!info.IsConstructCall())
    {
        Nan::ThrowError("Cannot call constructor as function, you need to use 'new' keyword");
        return;
    }

    if (info[0]->IsExternal())
    {
        Local<External> ext = Local<External>::Cast(info[0]);
        void* ptr = ext->Value();
        MemoryDatasource* d =  static_cast<MemoryDatasource*>(ptr);
        d->Wrap(info.This());
        info.GetReturnValue().Set(info.This());
        return;
    }
    if (info.Length() != 1){
        Nan::ThrowTypeError("accepts only one argument, an object of key:value datasource options");
        return;
    }

    if (!info[0]->IsObject())
    {
        Nan::ThrowTypeError("Must provide an object, eg {type: 'shape', file : 'world.shp'}");
        return;
    }

    Local<Object> options = info[0].As<Object>();

    mapnik::parameters params;
    Local<Array> names = options->GetPropertyNames();
    unsigned int i = 0;
    unsigned int a_length = names->Length();
    while (i < a_length) {
        Local<Value> name = names->Get(i)->ToString();
        Local<Value> value = options->Get(name);
        if (value->IsUint32() || value->IsInt32())
        {
            params[TOSTR(name)] = value->IntegerValue();
        }
        else if (value->IsNumber())
        {
            params[TOSTR(name)] = value->NumberValue();
        }
        else if (value->IsBoolean())
        {
            params[TOSTR(name)] = value->BooleanValue();
        }
        else
        {
            params[TOSTR(name)] = TOSTR(value);
        }
        i++;
    }
    params["type"] = "memory";
    //memory_datasource cache;
    MemoryDatasource* d = new MemoryDatasource();
    d->Wrap(info.This());
    d->datasource_ = std::make_shared<mapnik::memory_datasource>(params);
    info.GetReturnValue().Set(info.This());
}

Local<Value> MemoryDatasource::NewInstance(mapnik::datasource_ptr ds_ptr) {
    Nan::EscapableHandleScope scope;
    MemoryDatasource* d = new MemoryDatasource();
    d->datasource_ = ds_ptr;
    Local<Value> ext = Nan::New<External>(d);
    return scope.Escape( Nan::New(constructor)->GetFunction()->NewInstance(1, &ext));
}

NAN_METHOD(MemoryDatasource::parameters)
{
    MemoryDatasource* d = Nan::ObjectWrap::Unwrap<MemoryDatasource>(info.Holder());
    Local<Object> ds = Nan::New<Object>();
    if (d->datasource_) {
        mapnik::parameters::const_iterator it = d->datasource_->params().begin();
        mapnik::parameters::const_iterator end = d->datasource_->params().end();
        for (; it != end; ++it)
        {
            node_mapnik::params_to_object(ds, it->first, it->second);
        }
    }
    info.GetReturnValue().Set(ds);
}

NAN_METHOD(MemoryDatasource::describe)
{
    MemoryDatasource* d = Nan::ObjectWrap::Unwrap<MemoryDatasource>(info.Holder());
    Local<Object> description = Nan::New<Object>();
    if (d->datasource_) 
    {
        node_mapnik::describe_datasource(description,d->datasource_);
    }
    info.GetReturnValue().Set(description);
}

NAN_METHOD(MemoryDatasource::featureset)
{
    MemoryDatasource* d = Nan::ObjectWrap::Unwrap<MemoryDatasource>(info.Holder());

    if (d->datasource_) {
        mapnik::query q(d->datasource_->envelope());
        mapnik::layer_descriptor ld = d->datasource_->get_descriptor();
        std::vector<mapnik::attribute_descriptor> const& desc = ld.get_descriptors();
        std::vector<mapnik::attribute_descriptor>::const_iterator itr = desc.begin();
        std::vector<mapnik::attribute_descriptor>::const_iterator end = desc.end();
        while (itr != end)
        {
            // There is currently no way in the memory_datasource within mapnik to even
            // add a descriptor. Therefore it is impossible that this will ever be reached
            // currently.
            /* LCOV_EXCL_START */
            q.add_property_name(itr->get_name());
            ++itr;
            /* LCOV_EXCL_END */
        }
        mapnik::featureset_ptr fs = d->datasource_->features(q);
        if (fs)
        {
            info.GetReturnValue().Set(Featureset::NewInstance(fs));
        }
    }
    
    // Even if there is an empty query, a featureset is still created
    // therefore it should be impossible to reach this point in the code.
    /* LCOV_EXCL_START */
    return;
    /* LCOV_EXCL_END */
}

NAN_METHOD(MemoryDatasource::add)
{
    if ((info.Length() != 1) || !info[0]->IsObject())
    {
        Nan::ThrowError("accepts one argument: an object including x and y (or wkt) and properties");
        return;
    }

    MemoryDatasource* d = Nan::ObjectWrap::Unwrap<MemoryDatasource>(info.Holder());

    Local<Object> obj = info[0].As<Object>();

    if (obj->Has(Nan::New("wkt").ToLocalChecked()) || (obj->Has(Nan::New("x").ToLocalChecked()) && obj->Has(Nan::New("y").ToLocalChecked())))
    {
        if (obj->Has(Nan::New("wkt").ToLocalChecked()))
        {
            Nan::ThrowError("wkt not yet supported");
            return;
        }

        Local<Value> x = obj->Get(Nan::New("x").ToLocalChecked());
        Local<Value> y = obj->Get(Nan::New("y").ToLocalChecked());
        if (!x->IsUndefined() && x->IsNumber() && !y->IsUndefined() && y->IsNumber())
        {
            mapnik::context_ptr ctx = std::make_shared<mapnik::context_type>();
            mapnik::feature_ptr feature(mapnik::feature_factory::create(ctx,d->feature_id_));
            ++(d->feature_id_);
            feature->set_geometry(mapnik::geometry::point<double>(x->NumberValue(),y->NumberValue()));
            if (obj->Has(Nan::New("properties").ToLocalChecked()))
            {
                Local<Value> props = obj->Get(Nan::New("properties").ToLocalChecked());
                if (props->IsObject())
                {
                    Local<Object> p_obj = props->ToObject();
                    Local<Array> names = p_obj->GetPropertyNames();
                    unsigned int i = 0;
                    unsigned int a_length = names->Length();
                    while (i < a_length)
                    {
                        Local<Value> name = names->Get(i)->ToString();
                        // if name in q.property_names() ?
                        Local<Value> value = p_obj->Get(name);
                        if (value->IsString()) {
                            mapnik::value_unicode_string ustr = d->tr_.transcode(TOSTR(value));
                            feature->put_new(TOSTR(name),ustr);
                        } else if (value->IsNumber()) {
                            double num = value->NumberValue();
                            // todo - round
                            if (num == value->IntegerValue()) {
                                feature->put_new(TOSTR(name),static_cast<node_mapnik::value_integer>(value->IntegerValue()));
                            } else {
                                double dub_val = value->NumberValue();
                                feature->put_new(TOSTR(name),dub_val);
                            }
                        } else if (value->IsNull()) {
                            feature->put_new(TOSTR(name),mapnik::value_null());
                        }
                        i++;
                    }
                }
            }
            mapnik::memory_datasource *cache = dynamic_cast<mapnik::memory_datasource *>(d->datasource_.get());
            cache->push(feature);
            info.GetReturnValue().Set(Nan::True());
            return;
        }
    }
    info.GetReturnValue().Set(Nan::False());
}

NAN_METHOD(MemoryDatasource::fields)
{
    MemoryDatasource* d = Nan::ObjectWrap::Unwrap<MemoryDatasource>(info.Holder());
    Local<Object> fields = Nan::New<Object>();
    if (d->datasource_) {
        node_mapnik::get_fields(fields,d->datasource_);
    }
    info.GetReturnValue().Set(fields);
}
