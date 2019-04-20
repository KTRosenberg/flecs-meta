#include <include/meta.h>

ecs_vector_params_t cache_op_param;

static
ecs_vector_t* serialize_type(
    ecs_world_t *world,
    ecs_entity_t component,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles);

static
bool is_primitive(ecs_meta_cache_op_kind_t kind) {
    if (kind == EcsOpPrimitive || 
        kind == EcsOpEnum ||
        kind == EcsOpBitmask)
    {
        return true;
    } else {
        return false;
    }
}

static
bool is_inline(ecs_meta_cache_op_kind_t kind) {
    if (is_primitive(kind) || kind == EcsOpPush)
    {
        return true;
    } else {
        return false;
    }
}

static
ecs_vector_t* serialize_primitive(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    EcsMetaPrimitive *type = ecs_get_ptr(world, entity, EcsMetaPrimitive);
    ecs_assert(type != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_meta_cache_op_t *op = ecs_vector_add(&ops, &cache_op_param);
    *op = (ecs_meta_cache_op_t){
        .kind = EcsOpPrimitive, 
        .count = 1,
        .data.primitive_kind = type->kind
    };

    return ops;
}

static
ecs_vector_t* serialize_enum(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    EcsMetaEnum *type = ecs_get_ptr(world, entity, EcsMetaEnum);
    ecs_assert(type != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_meta_cache_op_t *op = ecs_vector_add(&ops, &cache_op_param);
    *op = (ecs_meta_cache_op_t){
        .kind = EcsOpEnum, 
        .count = 1,
        .data.enum_constants = type->constants
    };

    return ops;
}

static
ecs_vector_t* serialize_bitmask(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    EcsMetaBitmask *type = ecs_get_ptr(world, entity, EcsMetaBitmask);
    ecs_assert(type != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_meta_cache_op_t *op = ecs_vector_add(&ops, &cache_op_param);
    *op = (ecs_meta_cache_op_t){
        .kind = EcsOpBitmask,
        .count = 1,
        .data.enum_constants = type->constants
    };

    return ops;
}

static
ecs_vector_t* serialize_struct(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    EcsMetaStruct *type = ecs_get_ptr(world, entity, EcsMetaStruct);
    ecs_assert(type != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_meta_cache_op_t *op = ecs_vector_add(&ops, &cache_op_param);
    *op = (ecs_meta_cache_op_t){
        .kind = EcsOpPush
    };

    EcsMetaMember *members = ecs_vector_first(type->members);
    int32_t i, count = ecs_vector_count(type->members);

    for (i = 0; i < count; i ++) {
        ops = serialize_type(world, members[i].type, ops, handles);
        ecs_meta_cache_op_t *last_op = ecs_vector_last(ops, &cache_op_param);
        last_op->name = members[i].name;
    }

    op = ecs_vector_add(&ops, &cache_op_param);
    *op = (ecs_meta_cache_op_t){
        .kind = EcsOpPop
    };
    
    return ops;
}

static
ecs_vector_t* serialize_array(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    EcsMetaArray *type = ecs_get_ptr(world, entity, EcsMetaArray);
    ecs_assert(type != NULL, ECS_INTERNAL_ERROR, NULL);

    EcsMetaCache *element_cache = ecs_get_ptr(world, type->element_type, EcsMetaCache);
    ecs_assert(element_cache != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_meta_cache_op_t *elem_op = ecs_vector_first(element_cache->ops);

    /* If element is inlined, don't add indirection to other cache */
    if (ecs_vector_count(element_cache->ops) == 1 && is_inline(elem_op->kind))
    {
        /* Serialize element op inline, and set count on first inserted op */
        uint32_t el_start = ecs_vector_count(ops);
        serialize_type(world, type->element_type, ops, handles);
        ecs_meta_cache_op_t *start = ecs_vector_get(ops, &cache_op_param, el_start);
        ecs_assert(start != NULL, ECS_INTERNAL_ERROR, NULL);

        start->count = type->size;
    } else {
        ecs_meta_cache_op_t *op = ecs_vector_add(&ops, &cache_op_param);
        *op = (ecs_meta_cache_op_t){
            .kind = EcsOpArray, 
            .count = type->size,
            .data.element_ops = element_cache->ops
        };
    }

    return ops;
}

static
ecs_vector_t* serialize_vector(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    EcsMetaVector *type = ecs_get_ptr(world, entity, EcsMetaVector);
    ecs_assert(type != NULL, ECS_INTERNAL_ERROR, NULL);

    EcsMetaCache *element_cache = ecs_get_ptr(world, type->element_type, EcsMetaCache);
    ecs_assert(element_cache != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_meta_cache_op_t *op = ecs_vector_add(&ops, &cache_op_param);
    *op = (ecs_meta_cache_op_t){
        .kind = EcsOpVector, 
        .data.element_ops = element_cache->ops
    };

    return ops;
}

static
ecs_vector_t* serialize_map(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    EcsMetaMap *type = ecs_get_ptr(world, entity, EcsMetaMap);
    ecs_assert(type != NULL, ECS_INTERNAL_ERROR, NULL);

    EcsMetaCache *key_cache = ecs_get_ptr(world, type->key_type, EcsMetaCache);
    ecs_assert(key_cache != NULL, ECS_INTERNAL_ERROR, NULL);

    EcsMetaCache *value_cache = ecs_get_ptr(world, type->value_type, EcsMetaCache);
    ecs_assert(value_cache != NULL, ECS_INTERNAL_ERROR, NULL);

    /* Keys cannot be composite types */
    if (ecs_vector_count(key_cache->ops) != 1) {
        ecs_abort(ECS_INTERNAL_ERROR, NULL);
    }

    ecs_meta_cache_op_t *key_op = ecs_vector_first(key_cache->ops);

    /* Key types have to be primitive, enum or bitmask */
    ecs_assert(key_op->kind == EcsOpPrimitive ||
                key_op->kind == EcsOpEnum ||
                key_op->kind == EcsOpBitmask,
                    ECS_INTERNAL_ERROR, NULL);

    ecs_meta_cache_op_t *op = ecs_vector_add(&ops, &cache_op_param);
    *op = (ecs_meta_cache_op_t){
        .kind = EcsOpVector, 
        .data.map = {
            .key_op = key_op,
            .value_ops = value_cache->ops
        }
    };

    return ops;
}

static
ecs_vector_t* serialize_type(
    ecs_world_t *world,
    ecs_entity_t component,
    ecs_vector_t *ops,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    EcsMetaType *type = ecs_get_ptr(world, component, EcsMetaType);

    switch(type->kind) {
    case EcsPrimitive:
        ops = serialize_primitive(world, component, ops, handles);
        break;
    case EcsEnum:
        ops = serialize_enum(world, component, ops, handles);
        break;
    case EcsBitmask:
        ops = serialize_bitmask(world, component, ops, handles);
        break;
    case EcsStruct:
        ops = serialize_struct(world, component, ops, handles);
        break;
    case EcsArray:
        ops = serialize_array(world, component, ops, handles);
        break;
    case EcsVector:
        ops = serialize_vector(world, component, ops, handles);
        break;
    case EcsMap:
        ops = serialize_map(world, component, ops, handles);
        break;
    }

    return ops;
}

static
void serialize_component(
    ecs_world_t *world, 
    ecs_entity_t component,
    EcsComponentsMetaHandles *handles)
{
    EcsComponentsMeta_ImportHandles(*handles);

    ecs_vector_t *ops = ecs_vector_new(&cache_op_param, 1);
    ops = serialize_type(world, component, ops, handles);

    ecs_set(world, component, EcsMetaCache, {
        .ops = ops
    });
}

void InitCache(ecs_rows_t *rows) {
    ECS_IMPORT_COLUMN(rows, EcsComponentsMeta, 2);

    int i;
    for (i = 0; i < rows->count; i ++) {
        serialize_component(
            rows->world, rows->entities[i], &ecs_module(EcsComponentsMeta));
    }
}