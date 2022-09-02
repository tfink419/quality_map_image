require "bundler/gem_tasks"
require "rspec/core/rake_task"
require "rake/extensiontask"

Rake::ExtensionTask.new("quality_map_image") do |ext|
  ext.lib_dir = "lib/quality_map_image"
end

Rake.add_rakelib 'lib/tasks'

RSpec::Core::RakeTask.new(:spec)

task :default => %w(clean build install spec)