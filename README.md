# shelf
A place for things to collect dust due to incorrectness, incompleteness, or apparent uselessness

## concurrentQueue
I attempted to implement a specialized queue version of a [Harris lock-free linked list](https://timharris.uk/papers/2001-disc.pdf). I succeeded, only to discover that the data structure is purely academic and provides no option for memory reclamation. This sort of theoritical impracticality seems to be a frustrating trend in academia. Perhaps with more time I can determine a way to combine the two, but for now it sits on the shelf.
