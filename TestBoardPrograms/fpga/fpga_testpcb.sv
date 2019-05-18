module fpga_testpcb (
							input logic clk_in,
							input logic mcu_in,
							output logic clk_out,
							output logic mcu_out
						  );
	
	assign clk_out = clk_divide[24];
	assign mcu_out = mcu_in;
	
	reg [24:0] clk_divide;
	always @(posedge clk_in) 
	begin
		clk_divide <= clk_divide + 1;
	end

endmodule
	
						  
							