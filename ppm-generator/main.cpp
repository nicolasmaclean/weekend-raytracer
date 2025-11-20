#include <iostream>

int main()
{
    int width = 256;
    int height = 256;

    std::cout << "P3\n" << width << " " << height << "\n255\n";

    for (int y = 0; y < height; y++)
    {
	std::clog << "\rScanlines remaining: " << (height - y) << std::flush;
	for (int x = 0; x < width; x++)
	{
            auto r = double(x) / (width - 1);
	    auto g = double(y) / (height - 1);
	    auto b = 0.0;

	    int ir = int(255.0 * r);
	    int ig = int(255.0 * g);
	    int ib = int(255.0 * b);

	    std::cout << ir << " " << ig << " " << ib << "\n";
	}
    }

    std::clog << "\rDone!\n" << std::flush;
}
