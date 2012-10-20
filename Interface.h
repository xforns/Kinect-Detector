class Interface {
	
public:
	
	static Interface &instance();
	
	void update();
	void draw();

	
protected:

	static Interface _instance;
	
	
private:
	
	Interface();
	~Interface();
	
};
