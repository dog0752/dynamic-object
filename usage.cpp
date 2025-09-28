#include <iostream>

#include "dynobject.hpp"

int main()
{
	/* create the factory */
	dog0752::dynobj::ObjectFactory factory;

	/* intern some identifiers */
	dog0752::dynobj::ObjectFactory::Identifier id_name = factory.intern("name");
	dog0752::dynobj::ObjectFactory::Identifier id_sayHi =
		factory.intern("sayHi");

	/* create a new dynamic object */
	std::unique_ptr<dog0752::dynobj::ObjectFactory::DynObject> obj =
		factory.createObject();

	/* set a property */
	obj->set(factory, id_name, std::string("Cirno"));

	/* add a method */
	obj->set(factory, id_sayHi,
			 dog0752::dynobj::ObjectFactory::DynObject::Method(
				 [](dog0752::dynobj::ObjectFactory::DynObject &self,
					dog0752::dynobj::ObjectFactory::DynObject::Args /*args*/)
					 -> std::any
				 {
					 auto maybe_name =
						 self.get<std::string>(0); /* id 0 = "name" */
					 if (maybe_name.has_value())
					 {
						 return std::string("hello from ") + maybe_name.value();
					 }
					 return std::string("hello from ???");
				 }));

	/* call the method */
	auto result = obj->call<std::string>(id_sayHi);
	if (result.has_value())
	{
		std::cout << result.value() << "\n";
	}
	else
	{
		std::cerr << "error: " << result.error() << "\n";
	}

	return 0;
}
