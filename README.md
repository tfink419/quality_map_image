# Quality Map Image Builder Ruby C Extension

## About
This gem is a C extension to build the quality map images required for [Pitamap](https://www.pitamap.com)

## Why
Ruby was too slow for the dynamic image creation needed for [Pitamap](https://www.pitamap.com)

## Installation

Add this line to your application's Gemfile:

```ruby
gem 'quality_map_image', git: 'https://github.com/tfink419/quality_map_image.git'
```

And then execute:

    $ bundle install

Or install it yourself as:

    $ gem install quality_map_image

If this doesn't compile locally you will need to run the code through docker or figure out how to expose the libvips C libraries locally
## Usage

TODO: Write usage instructions here

## Development

After checking out the repo, run `bin/setup` to install dependencies. You can also run `bin/console` for an interactive prompt that will allow you to experiment.

Run `rake` to build and locally install the gem.
