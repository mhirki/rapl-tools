#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <vector>

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <file>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	FILE *fp = fopen(argv[1], "r");
	if (!fp) {
		fprintf(stderr, "Error: Failed to open file for reading!\n");
		exit(EXIT_FAILURE);
	}
	
	double value = 0.0;
	std::vector<double> values;
	double sum = 0.0;
	long num = 0;
	while (fscanf(fp, "%lf", &value) > 0) {
		sum += value;
		values.push_back(value);
		num++;
	}
	double avg = sum / num;
	printf("Average is %.9f\n", avg);
	
	// Calculate standard deviation
	double sum_squares = 0.0;
	int i = 0;
	for (i = 0; i < num; i++) {
		double diff = values[i] - avg;
		sum_squares += diff * diff;
	}
	double std_dev = sqrt(sum_squares / num);
	printf("Standard deviation is %.9f\n", std_dev);
	
	return 0;
}
