#include <functional>
#include <iostream>

void* g = nullptr;

struct C
{
	void doCb()
	{
		size_t dummy_a = 1;

		std::cout << "Outside: " << this << std::endl;
		std::function<void ()> f;
		f = [this, &dummy_a]() {};
		f = [this]() { g = this; std::cout << "Inside: " << this << std::endl; };
		f();
	}
};

int main()
{
	C c;
	c.doCb();
	return g == &c ? 0 : 77;
}

