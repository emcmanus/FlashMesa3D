package
{
	import cmodule.libGL.CLibInit;
	
	import flash.display.Bitmap;
	import flash.display.BitmapData;
	import flash.display.Sprite;
	import flash.events.Event;
	import flash.geom.Rectangle;
	import flash.utils.ByteArray;
	
	public class mesa3d extends Sprite
	{
		// Public
		public var screen:Bitmap;
		
		////////////////////////////////////////////////////////////
		
		// Video
		private var displayBufferAddress:Number;
		private var displayData:BitmapData;
		private var displayRect:Rectangle;
		
		// Application
		private var cLib:Object;
		private var cLoader:CLibInit;
		private var cMemory:ByteArray;
		
		// Config
		private var displayWidth:int = 400;
		private var displayHeight:int = 400;
		
		
		public function mesa3d()
		{
			var ram_NS:Namespace;
			
			// C Application
			cLoader = new CLibInit();
			
			cLib = cLoader.init();
			cLib.setup();
			
			// RAM
			ram_NS = new Namespace("cmodule.libGL");
			cMemory = (ram_NS::gstate).ds;
			
			// Display
			displayData = new BitmapData( displayWidth, displayHeight, false, 0x0 );
			displayRect = new Rectangle( 0, 0, displayWidth, displayHeight );
			
			screen = new Bitmap( displayData );
			
			// Call tick once
			cLib.tick();
			
			// Update display
			if (!displayBufferAddress)
			{
				displayBufferAddress = cLib.getDisplayPointer();
			}
			
			if (displayBufferAddress)
			{
				cMemory.position = displayBufferAddress;
				displayData.setPixels( displayRect, cMemory );
			}
		}
	}
}