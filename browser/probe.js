import { capabilities } from "./capabilities.js";

postMessage({ capabilities: capabilities(true) });
close();
