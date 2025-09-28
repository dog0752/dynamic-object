#include <iostream>
#include <chrono>
#include "dynobject.hpp"

int main()
{
	using namespace std::chrono;

	dog0752::dynobj::ObjectFactory factory;

	auto id_counter = factory.intern("counter");
	auto id_inc = factory.intern("inc");

	auto obj = factory.createObject();

	/* start with counter = 0 */
	obj->set(factory, id_counter, int(0));

	/* add a method: inc() { counter += 1; return counter; } */
	obj->set(
		factory, id_inc,
		dog0752::dynobj::ObjectFactory::DynObject::Method(
			[&](dog0752::dynobj::ObjectFactory::DynObject &self,
				dog0752::dynobj::ObjectFactory::DynObject::Args) -> std::any
			{
				auto val = self.get<int>(id_counter).value_or(0);
				val++;
				self.set(factory, id_counter, val);
				return val;
			}));

	constexpr int N = 1'000'000;

	auto start = high_resolution_clock::now();

	for (int i = 0; i < N; i++)
	{
		auto result = obj->call<int>(id_inc);
		if (!result.has_value())
		{
			std::cerr << "error: " << result.error() << "\n";
			return 1;
		}
	}

	auto end = high_resolution_clock::now();
	auto ms = duration_cast<milliseconds>(end - start).count();

	std::cout << "final counter = " << obj->get<int>(id_counter).value()
			  << "\n";
	std::cout << "did " << N << " calls in " << ms << " ms\n";
	std::cout << (1.0 * N / ms / 1000) << " million calls/sec approx\n";

	return 0;
}
