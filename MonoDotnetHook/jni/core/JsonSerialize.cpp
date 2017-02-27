#include <mono/metadata/appdomain.h>
#include <core/JsonSerialize.h>

JsonSerialize::JsonSerialize()
{
	domain = mono_domain_get();
}

void JsonSerialize::Serialize(MonoObject* obj, Json::Value &container)
{
	if (!obj)
	{
		return;
	}

	serialied_objs.clear();
	SerializeInner(obj, container);
}

void JsonSerialize::SerializeInner(MonoObject* obj, Json::Value& container)
{
	if (std::find(serialied_objs.begin(), serialied_objs.end(), obj) != serialied_objs.end())
	{
		return;
	}

	serialied_objs.push_back(obj);
	MonoClass *klass = mono_object_get_class(obj);
	do
	{
		if (CanSerializeClass(klass))
		{
			void *iter;

			iter = NULL;
			MonoClassField *field;
			while ((field = mono_class_get_fields(klass, &iter)))
			{
				if (CanSerializeField(field))
				{
					SerializeField(obj, field, container);
				}
			}

			iter = NULL;
			MonoProperty* prop;
			while ((prop = mono_class_get_properties(klass, &iter)))
			{
				if (CanSerializeProperty(prop))
				{
					SerializeProperty(obj, prop, container);
				}
			}

			klass = mono_class_get_parent(klass);
		}
		else
		{
			container = Json::Value((uint32_t)obj);
			break;
		}
	} while (klass);
}

void JsonSerialize::SerializeField(MonoObject*obj, MonoClassField* field, Json::Value& container)
{
	MonoType *mono_type = mono_field_get_type(field);
	const char *field_name = mono_field_get_name(field);
	uint32_t offset = mono_field_get_offset(field);
	void* start_addr = obj;
	uint32_t flags = mono_field_get_flags(field);
	if ((flags & FIELD_ATTRIBUTE_STATIC) && !(flags & FIELD_ATTRIBUTE_LITERAL))
	{
		start_addr = mono_vtable_get_static_field_data(mono_class_vtable(domain, mono_field_get_parent(field)));
	}
	SerializeMonoTypeWithAddr(mono_type, (void *)((uint64_t)start_addr + offset), container, field_name);
}

void JsonSerialize::SerializeProperty(MonoObject*obj, MonoProperty* prop, Json::Value& container)
{
	MonoMethod *getter = mono_property_get_get_method(prop);
	if (getter)
	{
		uint32_t flags = mono_method_get_flags(getter, NULL);
		if (!(flags & METHOD_ATTRIBUTE_ABSTRACT))
		{
			MonoMethodSignature *signature = mono_method_get_signature(getter, mono_class_get_image(mono_method_get_class(getter)), mono_method_get_token(getter));
			uint32_t param_count = mono_signature_get_param_count(signature);
			if (param_count == 0)
			{
				MonoObject *exception;
				MonoObject *ret = mono_runtime_invoke(getter, obj, NULL, &exception);
				if (!exception)
				{
					const char *prop_name = mono_property_get_name(prop);
					MonoType *ret_type = mono_signature_get_return_type(signature);
					SerializeMonoTypeWithAddr(ret_type, mono_type_is_reference(ret_type) ? &ret : mono_object_unbox(ret), container, Json::Value(prop_name));
				}
			}
		}
	}
}

bool JsonSerialize::CanSerializeClass(MonoClass* klass)
{
	return true;
}

bool JsonSerialize::CanSerializeField(_MonoClassField* field)
{
	return true;
}

bool JsonSerialize::CanSerializeProperty(MonoProperty* prop)
{
	return true;
}

void JsonSerialize::SerializeMonoTypeWithAddr(MonoType* mono_type, void* addr, Json::Value& container, Json::Value key)
{
	MonoTypeEnum type = (MonoTypeEnum)mono_type_get_type(mono_type);
	if (type == MONO_TYPE_I1)
	{
		json_advance_insert(container, key, Json::Value(*(int8_t *)addr));
	}
	else if (type == MONO_TYPE_U1 || type == MONO_TYPE_CHAR)
	{
		json_advance_insert(container, key, Json::Value(*(uint8_t *)addr));
	}
	else if (type == MONO_TYPE_I2)
	{
		json_advance_insert(container, key, Json::Value(*(int16_t *)addr));
	}
	else if (type == MONO_TYPE_U2)
	{
		json_advance_insert(container, key, Json::Value(*(uint16_t *)addr));
	}
	else if (type == MONO_TYPE_I4)
	{
		json_advance_insert(container, key, Json::Value(*(int32_t *)addr));
	}
	else if (type == MONO_TYPE_U4)
	{
		json_advance_insert(container, key, Json::Value(*(uint32_t *)addr));
	}
	else if (type == MONO_TYPE_I8)
	{
		json_advance_insert(container, key, Json::Value(*(int64_t *)addr));
	}
	else if (type == MONO_TYPE_U8)
	{
		json_advance_insert(container, key, Json::Value(*(uint64_t *)addr));
	}
	else if (type == MONO_TYPE_R4)
	{
		json_advance_insert(container, key, Json::Value(*(float *)addr));
	}
	else if (type == MONO_TYPE_R8)
	{
		json_advance_insert(container, key, Json::Value(*(double *)addr));
	}
	else if (type == MONO_TYPE_BOOLEAN)
	{
		json_advance_insert(container, key, Json::Value(*(bool *)addr));
	}
	else if (type == MONO_TYPE_OBJECT)
	{
		json_advance_insert(container, key, Json::Value(*(uint32_t *)addr));
	}
	else if (type == MONO_TYPE_STRING)
	{
		MonoString *mono_str_value = *(MonoString **)addr;
		if (mono_str_value)
		{
			json_advance_insert(container, key, Json::Value(mono_string_to_utf8(mono_str_value)));
		}
	}
	else if (type == MONO_TYPE_CLASS)
	{
		MonoObject *obj_value = *(MonoObject **)addr;
		if (obj_value)
		{
			Json::Value class_value;
			SerializeInner(obj_value, class_value);
			json_advance_insert(container, key, class_value);
		}
	}
	else if (type == MONO_TYPE_VALUETYPE)
	{
		if (mono_class_is_enum(mono_type_get_class(mono_type)))
		{
			char sztmp[256] = { 0x00 };
			enum_get_desc_by_value(mono_type, *(int32_t *)addr, sztmp, 256);
			json_advance_insert(container, key, Json::Value(sztmp));
		}
	}
	else if (type == MONO_TYPE_SZARRAY)
	{
		MonoArray *array = *(MonoArray **)addr;
		if (array && array->obj.vtable)
		{
			MonoClass *elem_class = mono_class_get_element_class(mono_class_from_mono_type(mono_type));
			int32_t elem_size = mono_class_array_element_size(elem_class);
			uint32_t count = array->max_length;

			for (uint32_t i = 0; i < count; i++)
			{
				SerializeMonoTypeWithAddr(mono_class_get_type(elem_class), mono_array_addr_with_size(array, elem_size, i), json_advance_get_memeber(container, key), Json::Value(i));
			}
		}
	}
	else if (type == MONO_TYPE_GENERICINST)
	{
		MonoGenericClass *generic_class = (MonoGenericClass *)mono_type_get_class(mono_type);
		MonoClass* container_class = generic_class->container_class;
		const char *container_class_name = mono_class_get_name(container_class);
		MonoType** mono_type_argv = generic_class->context.class_inst->type_argv;
		int32_t mono_type_argc = generic_class->context.class_inst->type_argc;

		if (strcmp(container_class_name, "List`1") == 0 && mono_type_argc == 1)
		{
			MonoObject *list_obj = *(MonoObject **)addr;
			if (list_obj)
			{
				MonoClass *list_generic_class = mono_class_from_mono_type(mono_type);
				MonoArray * items;
				MonoClassField *items_field = mono_class_get_field_from_name(list_generic_class, "_items");
				mono_field_get_value(list_obj, items_field, &items);
				uint32_t count = 0;
				MonoClassField *field_count = mono_class_get_field_from_name(list_generic_class, "_size");
				mono_field_get_value(list_obj, field_count, &count);
				uint32_t elem_size = mono_array_element_size(mono_object_get_class(&items->obj));
				;
				for (uint32_t i = 0; i < count; i++)
				{
					SerializeMonoTypeWithAddr(mono_type_argv[0], mono_array_addr_with_size(items, elem_size, i), json_advance_get_memeber(container, key), Json::Value(i));
				}
			}
		}
		else if (strcmp(container_class_name, "Dictionary`2") == 0 && mono_type_argc == 2)
		{
			MonoObject *dict_obj = *(MonoObject **)addr;
			if (dict_obj)
			{
				MonoArray *entries = NULL;
				MonoClass *dict_generic_class = mono_class_from_mono_type(mono_type);
				MonoClassField *entries_field = mono_class_get_field_from_name(dict_generic_class, "entries");
				if (entries_field)
				{
					mono_field_get_value(dict_obj, entries_field, &entries);
					if (entries)
					{
						int32_t align = 0;
						MonoObject *exception;
						uint32_t entry_size = mono_array_element_size(mono_object_get_class(&entries->obj));
						uint32_t key_size = mono_type_size(mono_type_argv[0], &align);
						MonoMethod *get_count = mono_class_get_method_from_name(dict_generic_class, "get_Count", 0);
						uint32_t count = *(uint32_t *)mono_object_unbox(mono_runtime_invoke(get_count, dict_obj, NULL, &exception));
						if (!exception)
						{
							for (uint32_t i = 0; i < count; i++)
							{
								char* entry = mono_array_addr_with_size(entries, entry_size, i);
								Json::Value tmp;
								SerializeMonoTypeWithAddr(mono_type_argv[0], entry + 8, tmp, "key");
								SerializeMonoTypeWithAddr(mono_type_argv[1], entry + 8 + key_size, tmp, "value");
								json_advance_insert(json_advance_get_memeber(container, key), tmp["key"], tmp["value"]);
							}
						}
					}
				}
				else
				{
					MonoClassField *key_field = mono_class_get_field_from_name(dict_generic_class, "keySlots");
					MonoClassField *value_field = mono_class_get_field_from_name(dict_generic_class, "valueSlots");
					if (key_field && value_field)
					{
						MonoObject *exception;
						MonoMethod *get_count = mono_class_get_method_from_name(dict_generic_class, "get_Count", 0);
						MonoObject *count_obj = mono_runtime_invoke(get_count, dict_obj, NULL, &exception);
						if (!exception && count_obj)
						{
							uint32_t count = *(uint32_t *)mono_object_unbox(count_obj);
							MonoArray *key_array = NULL;
							MonoArray *value_array = NULL;
							mono_field_get_value(dict_obj, key_field, &key_array);
							mono_field_get_value(dict_obj, value_field, &value_array);
							if (key_array && value_array)
							{
								int32_t align = 0;
								uint32_t key_size = mono_type_size(mono_type_argv[0], &align);
								uint32_t value_size = mono_type_size(mono_type_argv[1], &align);
								for (uint32_t i = 0; i < count; i++)
								{
									char* key_addr = mono_array_addr_with_size(key_array, key_size, i);
									char* value_addr = mono_array_addr_with_size(value_array, value_size, i);
									Json::Value tmp;
									SerializeMonoTypeWithAddr(mono_type_argv[0], key_addr, tmp, "key");
									SerializeMonoTypeWithAddr(mono_type_argv[1], value_addr, tmp, "value");
									json_advance_insert(json_advance_get_memeber(container, key), tmp["key"], tmp["value"]);
								}
							}
						}

					}
				}
			}
		}
		else
		{
			if (mono_type_is_reference(mono_type))
			{
				MonoObject *obj_value = *(MonoObject **)addr;
				if (obj_value)
				{
					Json::Value class_value;
					SerializeInner(obj_value, class_value);
					json_advance_insert(container, key, class_value);
				}
			}
			else
			{
				SerializeMonoTypeWithAddr(mono_class_get_type(container_class), addr, container, key);
			}
		}
	}
}

MonoObject* JsonSerialize::Deserialize(Json::Value container, MonoClass* klass)
{
	if (!klass)
	{
		return NULL;
	}

	if (container == Json::nullValue)
	{
		return NULL;
	}

	return DeserializeInner(container, klass);
}

MonoObject* JsonSerialize::DeserializeInner(Json::Value container, MonoClass* klass)
{
	MonoObject *obj = mono_object_new(domain, klass);
	do
	{
		if (CanSerializeClass(klass))
		{
			void *iter = NULL;

			iter = NULL;
			MonoClassField *field;
			while ((field = mono_class_get_fields(klass, &iter)))
			{
				if (CanSerializeField(field))
				{
					DeserializeField(obj, field, container);
				}
			}

			iter = NULL;
			MonoProperty* prop;
			while ((prop = mono_class_get_properties(klass, &iter)))
			{
				if (CanSerializeProperty(prop))
				{
					DeserializeProperty(obj, prop, container);
				}
			}
			klass = mono_class_get_parent(klass);
		}
		else
		{
			obj = (MonoObject *)container.asUInt();
			break;
		}
	} while (klass);

	return obj;
}

void JsonSerialize::DeserializeField(MonoObject* obj, MonoClassField* field, Json::Value container)
{
	const char *field_name = mono_field_get_name(field);
	if (!json_advance_isnull(container[field_name]))
	{
		MonoType *mono_type = mono_field_get_type(field);
		uint32_t offset = mono_field_get_offset(field);
		void* start_addr = obj;
		uint32_t flags = mono_field_get_flags(field);
		if ((flags & FIELD_ATTRIBUTE_STATIC) && !(flags & FIELD_ATTRIBUTE_LITERAL))
		{
			start_addr = mono_vtable_get_static_field_data(mono_class_vtable(domain, mono_field_get_parent(field)));
		}
		DeserializeMonoTypeWithAddr(mono_type, (void *)((uint64_t)start_addr + offset), container[field_name]);
	}
}

void JsonSerialize::DeserializeProperty(MonoObject* obj, MonoProperty* prop, Json::Value container)
{
	const char *prop_name = mono_property_get_name(prop);
	if (!json_advance_isnull(container[prop_name]))
	{
		MonoMethod *setter = mono_property_get_set_method(prop);
		if (setter)
		{
			uint32_t flags = mono_method_get_flags(setter, NULL);
			if (!(flags & METHOD_ATTRIBUTE_ABSTRACT))
			{
				MonoMethodSignature *signature = mono_method_get_signature(setter, mono_class_get_image(mono_method_get_class(setter)), mono_method_get_token(setter));
				uint32_t param_count = mono_signature_get_param_count(signature);
				if (param_count == 1)
				{
					int32_t align = 0;
					void *iter = NULL;
					MonoObject *exception;
					MonoType *param_type = mono_signature_get_params(signature, &iter);
					int32_t param_size = mono_type_size(param_type, &align);
					void *value_addr = malloc(param_size);
					void *args[1] = { value_addr };
					DeserializeMonoTypeWithAddr(param_type, value_addr, container[prop_name]);
					args[0] = mono_type_is_reference(param_type) ? *(void **)value_addr : value_addr;
					mono_runtime_invoke(setter, obj, args, &exception);
					free(value_addr);
				}
			}
		}
	}
}

void JsonSerialize::DeserializeMonoTypeWithAddr(MonoType* mono_type, void* addr, Json::Value container)
{
	uint32_t type = mono_type_get_type(mono_type);
	if (type == MONO_TYPE_I1)
	{
		*(int8_t *)addr = container.asInt();
	}
	else if (type == MONO_TYPE_U1 || type == MONO_TYPE_CHAR)
	{
		*(uint8_t *)addr = container.asUInt();
	}
	else if (type == MONO_TYPE_I2)
	{
		*(int16_t *)addr = container.asInt();
	}
	else if (type == MONO_TYPE_U2)
	{
		*(uint16_t *)addr = container.asUInt();
	}
	else if (type == MONO_TYPE_I4)
	{
		*(int32_t *)addr = container.asInt();
	}
	else if (type == MONO_TYPE_U4)
	{
		*(uint32_t *)addr = container.asUInt();
	}
	else if (type == MONO_TYPE_I8)
	{
		*(int64_t *)addr = container.asInt64();
	}
	else if (type == MONO_TYPE_U8)
	{
		*(uint64_t *)addr = container.asUInt64();
	}
	else if (type == MONO_NATIVE_R4)
	{
		*(float *)addr = (float)container.asDouble();
	}
	else if (type == MONO_TYPE_R8)
	{
		*(double *)addr = container.asDouble();
	}
	else if (type == MONO_TYPE_BOOLEAN)
	{
		*(bool *)addr = container.asBool();
	}
	else if (type == MONO_TYPE_STRING)
	{
		*(MonoString **)addr = mono_string_new(domain, container.asCString());
	}
	else if (type == MONO_TYPE_OBJECT)
	{
		*(MonoObject **)addr = (MonoObject *)container.asUInt64();
	}
	else if (type == MONO_TYPE_VALUETYPE)
	{
		if (mono_class_is_enum(mono_type_get_class(mono_type)))
		{
			*(uint32_t *)addr = enum_get_value_by_desc(mono_type, container.asCString());
		}
	}
	else if (type == MONO_TYPE_CLASS)
	{
		if (!container.isNull())
		{
			*(MonoObject **)addr = DeserializeInner(container, mono_class_from_mono_type(mono_type));
			Json::FastWriter writer;
		}
	}
	else if (type == MONO_TYPE_SZARRAY)
	{
		uint32_t count = container.size();
		if (count > 0)
		{
			MonoClass *elem_class = mono_class_get_element_class(mono_class_from_mono_type(mono_type));
			MonoArray *array = mono_array_new(domain, elem_class, count);
			int32_t elem_size = mono_class_array_element_size(elem_class);

			for (uint32_t i = 0; i < count; i++)
			{
				DeserializeMonoTypeWithAddr(mono_class_get_type(elem_class), mono_array_addr_with_size(array, elem_size, i), container[i]);
			}
			*(MonoArray **)addr = array;
		}
	}
	else if (type == MONO_TYPE_GENERICINST)
	{
		MonoGenericClass *generic_class = (MonoGenericClass *)mono_type_get_class(mono_type);
		MonoClass* container_class = generic_class->container_class;
		const char *container_class_name = mono_class_get_name(container_class);
		MonoType** mono_type_argv = generic_class->context.class_inst->type_argv;
		int32_t mono_type_argc = generic_class->context.class_inst->type_argc;

		if (strcmp(container_class_name, "List`1") == 0 && mono_type_argc == 1)
		{
			uint32_t count = container.size();
			if (count > 0)
			{
				MonoClass *list_generic_class = mono_class_from_mono_type(mono_type);
				MonoObject *list_obj = mono_object_new(domain, list_generic_class);
				MonoClassField *field_items = mono_class_get_field_from_name(list_generic_class, "_items");
				MonoClass *elem_class = mono_class_get_element_class(mono_class_from_mono_type(mono_field_get_type(field_items)));
				MonoArray * items = mono_array_new(domain, elem_class, count);
				mono_field_set_value(list_obj, field_items, items);
				MonoClassField *field_count = mono_class_get_field_from_name(list_generic_class, "_size");
				mono_field_set_value(list_obj, field_count, &count);
				int32_t elem_size = mono_class_array_element_size(elem_class);

				for (uint32_t i = 0; i < count; i++)
				{
					DeserializeMonoTypeWithAddr(mono_type_argv[0], mono_array_addr_with_size(items, elem_size, i), container[i]);
				}
				*(MonoObject **)addr = list_obj;
			}
		}
		else if (strcmp(container_class_name, "Dictionary`2") == 0 && mono_type_argc == 2)
		{
			uint32_t count = container.size();
			if (count > 0)
			{
				int32_t align = 0;
				MonoClass *dict_generic_class = mono_class_from_mono_type(mono_type);
				MonoObject *dict_obj = mono_object_new(domain, dict_generic_class);
				mono_runtime_object_init(dict_obj);
				MonoMethod *add_method = mono_class_get_method_from_name(dict_generic_class, "Add", 2);
				uint32_t key_size = mono_type_size(mono_type_argv[0], &align);
				uint32_t value_size = mono_type_size(mono_type_argv[1], &align);
				void *key = malloc(key_size);
				void *value = malloc(value_size);
				void *args[2] = { 0x00 };
				MonoObject *exception;
				Json::Value::Members members = container.getMemberNames();
				for (uint32_t i = 0; i < count; i++)
				{
					DeserializeMonoTypeWithAddr(mono_type_argv[0], key, Json::Value(members[i]));
					DeserializeMonoTypeWithAddr(mono_type_argv[1], value, container.get(members[i], Json::nullValue));
					args[0] = mono_type_is_reference(mono_type_argv[0]) ? *(void **)key : key;
					args[1] = mono_type_is_reference(mono_type_argv[1]) ? *(void **)value : value;
					mono_runtime_invoke(add_method, dict_obj, args, &exception);
				}
				free(key);
				free(value);
				*(MonoObject **)addr = dict_obj;
			}
		}
		else
		{
			if (!container.isNull())
			{
				if (mono_type_is_reference(mono_type))
				{
					*(MonoObject **)addr = DeserializeInner(container, mono_class_from_mono_type(mono_type));
				}
				else
				{
					DeserializeMonoTypeWithAddr(mono_class_get_type(container_class), addr, container);
				}
			}
		}
	}
}

void JsonSerialize::enum_get_desc_by_value(MonoType* mono_type, int32_t enum_value, char* result, uint32_t size)
{
	void *iter = NULL;
	MonoClassField *field;
	MonoClass *type_class = mono_type_get_class(mono_type);

	while ((field = mono_class_get_fields(type_class, &iter)))
	{
		if (mono_field_is_static(field))
		{
			const char *class_name = mono_class_get_name(type_class);
			const char *type_name = mono_field_get_name(field);
			uint32_t field_value = 0;
			MonoVTable *vtable = mono_class_vtable(mono_domain_get(), type_class);
			mono_field_static_get_value(vtable, field, &field_value);
			if (field_value == enum_value)
			{
				snprintf(result, size, "%s.%s", class_name, type_name);
				break;
			}
		}
	}
}

uint32_t JsonSerialize::enum_get_value_by_desc(MonoType* mono_type, const char* desc)
{
	uint32_t ret = 0;
	void *iter = NULL;
	MonoClassField *field;
	MonoClass *type_class = mono_class_from_mono_type(mono_type);

	while ((field = mono_class_get_fields(type_class, &iter)))
	{
		if (mono_field_is_static(field))
		{
			char sztmp[256] = { 0x00 };
			uint32_t field_value = 0;
			MonoVTable *vtable = mono_class_vtable(mono_domain_get(), type_class);
			mono_field_static_get_value(vtable, field, &field_value);
			snprintf(sztmp, 256, "%s.%s", mono_class_get_name(type_class), mono_field_get_name(field));
			if (strcmp(desc, sztmp) == 0)
			{
				ret = field_value;
				break;
			}
		}
	}

	return ret;
}

void JsonSerialize::json_advance_insert(Json::Value &container, Json::Value key, Json::Value value)
{
	if (key.isIntegral())
	{
		container[key.asUInt()] = value;
	}
	else if (key.isString())
	{
		container[key.asCString()] = value;
	}
	else if (key.isObject())
	{
		Json::FastWriter writer;
		container[writer.write(key)] = value;
	}
}

Json::Value &JsonSerialize::json_advance_get_memeber(Json::Value &container, Json::Value key)
{
	if (key.isIntegral())
	{
		return container[key.asUInt()];
	}
	else
	{
		return container[key.asCString()];
	}
}

bool JsonSerialize::json_advance_isnull(Json::Value container)
{
	if (container == Json::nullValue)
	{
		return true;
	}

	if (container.isArray())
	{
		bool is_all_null = true;
		for (uint32_t i = 0; i < container.size(); i++)
		{
			if (container[i] != Json::nullValue)
			{
				is_all_null = false;
				break;
			}
		}

		return is_all_null;
	}

	return false;
}
